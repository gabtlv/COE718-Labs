/*----------------------------------------------------------------------------
 * COE718 Lab 3a - Question 1: Round-robin scheduling with three tasks
 *
 * Theme: a board "Power-On Self-Test" (POST). Three independent self-tests
 * share the CPU under RTX round-robin scheduling (15 ms time slice, see
 * RTX_Conf_CM.c). Each test has a FINITE workload: it runs its checks,
 * reports PASS/FAIL, and then deletes itself. Once all three are done only
 * the idle demon is left, so countIDLE starts to increase.
 *
 *   tRAM   - RAM march test   : write / verify / invert a 1 KB buffer
 *   tPrime - ALU test         : count the primes up to 20000 (expect 2262)
 *   tCRC   - data integrity   : CRC-32 of a 512-byte block, repeated; every
 *                               round must give the same checksum
 *
 * Watch 1: ramPass ramErrors primeCount crcRound crcValue crcErrors
 *          testsDone postPassed countIDLE
 *
 * Build switch (build_cfg.h): DEMO 0 = analysis version (simulator, no
 * LED/LCD code). DEMO 1 = demo version (LEDs + LCD show the running thread).
 *---------------------------------------------------------------------------*/
#include "cmsis_os.h"
#include "build_cfg.h"
#include "demo_io.h"

/*----------------------------- workload size ------------------------------*/
#define RAM_WORDS      256u          /* 1 KB test buffer                     */
#define RAM_PASSES     300u          /* march passes                         */
#define PRIME_LIMIT    20000u        /* count primes in 2..PRIME_LIMIT       */
#define PRIME_EXPECT   2262u         /* known answer for 20000               */
#define PRIME_STEP     (PRIME_LIMIT / 100u)
#define CRC_BYTES      512u          /* size of the data block               */
#define CRC_ROUNDS     100u          /* checksum repetitions                 */
#define DEMO_STEP_MS   30u           /* demo only: slow-down per 1% progress */

/*------------------------- results (Watch 1) ------------------------------*/
volatile uint32_t ramPass      = 0;  /* completed march passes               */
volatile uint32_t ramErrors    = 0;  /* mismatches found (expect 0)          */
volatile uint32_t primeChecked = 0;  /* last number tested                   */
volatile uint32_t primeCount   = 0;  /* primes found (expect 2262)           */
volatile uint32_t crcRound     = 0;  /* completed CRC rounds                 */
volatile uint32_t crcValue     = 0;  /* CRC-32 of the data block             */
volatile uint32_t crcErrors    = 0;  /* rounds that disagreed (expect 0)     */
volatile uint32_t testsDone    = 0;  /* finished tests (0..3)                */
volatile uint32_t testsPassed  = 0;  /* passed tests (0..3)                  */
volatile uint32_t postPassed   = 0;  /* 1 when all three tests passed        */

static uint32_t ram_buf[RAM_WORDS];
static uint8_t  crc_data[CRC_BYTES];

/* testsDone / testsPassed are updated by all three threads: a time-slice
   switch in the middle of "testsDone++" could lose an update, so the
   read-modify-write is protected by a mutex. */
osMutexDef(result_mutex);
static osMutexId result_mtx;

/*---------------------------- thread objects ------------------------------*/
void tRAM   (void const *argument);
void tPrime (void const *argument);
void tCRC   (void const *argument);

osThreadDef(tRAM,   osPriorityNormal, 1, 0);
osThreadDef(tPrime, osPriorityNormal, 1, 0);
osThreadDef(tCRC,   osPriorityNormal, 1, 0);

osThreadId tid_RAM, tid_Prime, tid_CRC;

/*------------------- demo only: visible slow-down -------------------------*/
#if DEMO
/* Busy-wait (not osDelay) so the thread keeps using its time slice; the LED
   is refreshed while waiting so it always shows the thread that runs now. */
static void demo_pause (int led) {
  uint32_t t;
  for (t = 0; t < DEMO_STEP_MS; t++) {
    LED_Show(led);
    Demo_Spin(1);
  }
}
#else
#define demo_pause(led)   ((void)0)
#endif

/*------------------------------ helpers -----------------------------------*/
static int is_prime (uint32_t n) {
  uint32_t d;
  if (n < 2u)        return 0;
  if ((n & 1u) == 0) return (n == 2u);
  for (d = 3u; d * d <= n; d += 2u) {
    if (n % d == 0u) return 0;
  }
  return 1;
}

static uint32_t crc32 (const uint8_t *p, uint32_t len) {
  uint32_t c = 0xFFFFFFFFu;
  int k;
  while (len--) {
    c ^= *p++;
    for (k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
  }
  return ~c;
}

/* Common end of every test: record the result, then delete the thread. */
static void test_finished (unsigned int ln, const char *pass_txt,
                           const char *fail_txt, int ok) {
  uint32_t done;

  osMutexWait(result_mtx, osWaitForever);
  testsDone++;
  if (ok) testsPassed++;
  done = testsDone;
  if (done == 3u) postPassed = (testsPassed == 3u);
  osMutexRelease(result_mtx);

  LCD_Text(ln, ok ? Green : Red, ok ? pass_txt : fail_txt);
  if (done == 3u) {                                  /* last test to finish */
    LCD_Text(8, White, "Running: idle");
    LCD_Text(9, postPassed ? Green : Red,
             postPassed ? "POST PASSED  3/3" : "POST FAILED");
    LED_Set(postPassed ? 0xFFu : 0x00u);
  }
  osThreadTerminate(osThreadGetId());               /* finite: delete self */
}

/*------------------------------ threads -----------------------------------*/
/* Thread 1: RAM march test (LED0, LCD lines 1-2) */
void tRAM (void const *argument) {
  uint32_t pass, i, pattern;

  LCD_Text(1, White, "1 RAM march  : RUN");
  for (pass = 0; pass < RAM_PASSES; pass++) {
    pattern = 0xA5A5A5A5u ^ (pass * 0x01010101u);
    for (i = 0; i < RAM_WORDS; i++) {                /* up:   write pattern  */
      ram_buf[i] = pattern ^ i;
    }
    for (i = 0; i < RAM_WORDS; i++) {                /* up:   read, invert   */
      if (ram_buf[i] != (pattern ^ i)) ramErrors++;
      ram_buf[i] = ~(pattern ^ i);
    }
    for (i = RAM_WORDS; i-- > 0u; ) {                /* down: read inverse   */
      if (ram_buf[i] != ~(pattern ^ i)) ramErrors++;
    }
    ramPass = pass + 1u;
    LED_Show(0);

    if ((ramPass % (RAM_PASSES / 100u)) == 0u) {     /* every 1 %            */
      LCD_Text(8, White, "Running: RAM");
      LCD_Bar(2, Cyan, (ramPass * 100u) / RAM_PASSES);
      demo_pause(0);
    }
  }
  test_finished(1, "1 RAM march  : PASS", "1 RAM march  : FAIL",
                ramErrors == 0u);
}

/* Thread 2: ALU / divider test - count primes (LED1, LCD lines 3-4) */
void tPrime (void const *argument) {
  uint32_t n;

  LCD_Text(3, White, "2 ALU primes : RUN");
  for (n = 2u; n <= PRIME_LIMIT; n++) {
    if (is_prime(n)) primeCount++;
    primeChecked = n;
    LED_Show(1);

    if ((n % PRIME_STEP) == 0u) {                    /* every 1 %            */
      LCD_Text(8, White, "Running: ALU");
      LCD_Bar(4, Magenta, (n * 100u) / PRIME_LIMIT);
      demo_pause(1);
    }
  }
  test_finished(3, "2 ALU primes : PASS", "2 ALU primes : FAIL",
                primeCount == PRIME_EXPECT);
}

/* Thread 3: data integrity - repeated CRC-32 (LED2, LCD lines 5-6) */
void tCRC (void const *argument) {
  uint32_t r, v, ref = 0;

  LCD_Text(5, White, "3 CRC-32     : RUN");
  for (r = 0; r < CRC_ROUNDS; r++) {
    v = crc32(crc_data, CRC_BYTES);
    if (r == 0u)       ref = v;                      /* first result = ref   */
    else if (v != ref) crcErrors++;
    crcValue = v;
    crcRound = r + 1u;
    LED_Show(2);

    if ((crcRound % (CRC_ROUNDS / 100u)) == 0u) {    /* every 1 %            */
      LCD_Text(8, White, "Running: CRC");
      LCD_Bar(6, Yellow, (crcRound * 100u) / CRC_ROUNDS);
      demo_pause(2);
    }
  }
  test_finished(5, "3 CRC-32     : PASS", "3 CRC-32     : FAIL",
                crcErrors == 0u);
}

/*-------------------------------- main ------------------------------------*/
int main (void) {
  uint32_t i;

  osKernelInitialize();                              /* initialize RTX       */

  for (i = 0; i < CRC_BYTES; i++) {                  /* fixed test data      */
    crc_data[i] = (uint8_t)(i * 7u + 3u);
  }
  result_mtx = osMutexCreate(osMutex(result_mutex));
  Demo_Init("Q1 RR POST  15 ms");

  tid_RAM   = osThreadCreate(osThread(tRAM),   NULL); /* created first ->   */
  tid_Prime = osThreadCreate(osThread(tPrime), NULL); /* runs first         */
  tid_CRC   = osThreadCreate(osThread(tCRC),   NULL);

  osKernelStart();                                   /* start thread switching */
  osDelay(osWaitForever);                            /* main has nothing left  */
}
