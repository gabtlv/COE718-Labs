/*----------------------------------------------------------------------------
 * COE718 Lab 3a - Question 2: Round-robin scheduling of an OS problem
 *
 * Five equal-priority threads (round-robin, see RTX_Conf_CM.c). Each has its
 * own global variable and a finite workload, and deletes itself when done.
 *
 *  Memory Management : ++memAccessCount, bit-band computation on a volatile
 *                      word in the AHB SRAM bit-band region, passes control
 *                      to CPU Management (signal), waits for CPU Management
 *                      to signal back, delays one tick, deletes itself.
 *  CPU Management    : waits for Memory Management, ++cpuAccessCount, runs a
 *                      conditional-execution + barrel-shifter hardware check,
 *                      signals Memory Management, deletes itself.
 *  Application IF    : takes the logger mutex, writes the first part of the
 *                      message, then lets Device Management run (so the App
 *                      always runs before Device), waits for Device to finish
 *                      the message, ++appCounter, delays one tick, deletes.
 *  Device Management : waits for the App, takes the logger mutex, writes the
 *                      ending of the message, signals the App, ++devCounter,
 *                      delays one tick ("file closed"), deletes itself.
 *  User Interface    : ++userCount, delays one tick, deletes itself.
 *
 * main() starts thread execution with Memory Management (created first).
 *
 * Watch 1: memAccessCount memWord memBitResult cpuAccessCount cpuHwOK
 *          condResult barrelResult appCounter devCounter userCount logger
 *          countIDLE
 *
 * Build switch (build_cfg.h): DEMO 0 = analysis version (simulator, no
 * LED/LCD code). DEMO 1 = demo version (LEDs + LCD show the running thread).
 *---------------------------------------------------------------------------*/
#include "cmsis_os.h"
#include "LPC17xx.h"
#include "build_cfg.h"
#include "demo_io.h"

/*----------------------------- signal flags -------------------------------*/
#define SIG_CPU_GO    0x01            /* Memory -> CPU    : "your turn"       */
#define SIG_MEM_ACK   0x02            /* CPU    -> Memory : "done"            */
#define SIG_DEV_GO    0x04            /* App    -> Device : "finish the log"  */
#define SIG_APP_ACK   0x08            /* Device -> App    : "log complete"    */

#define DEMO_HOLD_MS  1500u           /* demo only: time each thread stays lit */

/*------------------------ globals (one per thread) ------------------------*/
volatile uint32_t memAccessCount = 0;   /* Memory Management                */
volatile uint32_t memBitResult   = 0;   /* word read back after bit-banding */
volatile uint32_t cpuAccessCount = 0;   /* CPU Management                   */
volatile int32_t  condResult     = 0;   /* conditional-execution result     */
volatile int32_t  barrelResult   = 0;   /* barrel-shifter result            */
volatile uint32_t cpuHwOK        = 0;   /* 1 = asm and C results agree      */
volatile uint32_t appCounter     = 0;   /* Application Interface            */
volatile uint32_t devCounter     = 0;   /* Device Management                */
volatile uint32_t userCount      = 0;   /* User Interface: number of users  */
char              logger[48]     = "";  /* shared log, protected by mutex   */

/* Bit banding only works in the SRAM bit-band region (0x20000000-0x200FFFFF).
   The LPC1768 main SRAM (0x10000000) is outside it, so this word is placed
   in the AHB SRAM bank at 0x2007C000. */
volatile uint32_t memWord __attribute__((at(0x2007C000))) = 0x5A5A5A5Au; /* cleared at run time */

#define BITBAND_SRAM(addr, bit) \
  (*(volatile uint32_t *)(0x22000000u + (((uint32_t)(addr) - 0x20000000u) << 5) + ((bit) << 2)))

osMutexDef(log_mutex);
static osMutexId log_mtx;

/*---------------------------- thread objects ------------------------------*/
void MemoryManagement (void const *argument);
void CPUManagement    (void const *argument);
void AppInterface     (void const *argument);
void DeviceManagement (void const *argument);
void UserInterface    (void const *argument);

osThreadDef(MemoryManagement, osPriorityNormal, 1, 0);
osThreadDef(CPUManagement,    osPriorityNormal, 1, 0);
osThreadDef(AppInterface,     osPriorityNormal, 1, 0);
osThreadDef(DeviceManagement, osPriorityNormal, 1, 0);
osThreadDef(UserInterface,    osPriorityNormal, 1, 0);

osThreadId tid_Mem, tid_CPU, tid_App, tid_Dev, tid_User;

/*------------------- demo only: show the running thread -------------------*/
#if DEMO
/* LED n and LCD line n+1 belong to thread n:
   0 Memory, 1 CPU, 2 App, 3 Device, 4 User */
static void show_state (int idx, unsigned short color, const char *txt) {
  LCD_Text((unsigned int)idx + 1u, color, txt);
}
/* Busy-wait (not osDelay) so the thread keeps the CPU for its time slices;
   the LED is refreshed while waiting so it always shows who runs now. */
static void demo_hold (int idx) {
  uint32_t t;
  for (t = 0; t < DEMO_HOLD_MS; t++) {
    LED_Show(idx);
    Demo_Spin(1);
  }
}
#else
#define show_state(idx, color, txt)   ((void)0)
#define demo_hold(idx)                ((void)0)
#endif

/*----------------------- logger helper (mutex held) -----------------------*/
static void log_append (const char *s) {
  uint32_t n = 0;
  while (logger[n] != 0 && n < sizeof(logger) - 1u) n++;
  while (*s != 0 && n < sizeof(logger) - 1u) logger[n++] = *s++;
  logger[n] = 0;
}

/*------------------ CPU hardware check (Lab 2 features) -------------------*/
/* Conditional execution + barrel shifter in assembly:
     d = (a > b) ? (a - b) : (b - a);   -> IT block, no branch
     return d + (b << 2);               -> shift folded into the ADD        */
__asm int32_t cpu_hw_check (int32_t a, int32_t b) {
  CMP     r0, r1
  ITE     GT
  SUBGT   r2, r0, r1                  ; executes only if a >  b
  SUBLE   r2, r1, r0                  ; executes only if a <= b
  ADD     r0, r2, r1, LSL #2          ; r0 = d + (b << 2), one instruction
  BX      lr
}

/*------------------------------ threads -----------------------------------*/
void MemoryManagement (void const *argument) {
  show_state(0, Green, "Memory  : RUN");
  memAccessCount++;

  /* bit-band computation on a volatile memory word: set/clear single bits
     through their alias addresses (one store each, no read-modify-write) */
  memWord = 0;
  BITBAND_SRAM(&memWord, 0)  = 1;
  BITBAND_SRAM(&memWord, 4)  = 1;
  BITBAND_SRAM(&memWord, 31) = 1;
  BITBAND_SRAM(&memWord, (memAccessCount & 7u) + 8u) = 1; /* bit 9 here     */
  BITBAND_SRAM(&memWord, 4)  = 0;
  memBitResult = memWord;                            /* expect 0x80000201    */
  demo_hold(0);

  show_state(0, Yellow, "Memory  : wait CPU");
  osSignalSet(tid_CPU, SIG_CPU_GO);                  /* pass control to CPU  */
  osSignalWait(SIG_MEM_ACK, osWaitForever);          /* wait for CPU's reply */

  osDelay(1);                                        /* delay one tick       */
  show_state(0, DarkGrey, "Memory  : done");
  osThreadTerminate(osThreadGetId());
}

void CPUManagement (void const *argument) {
  int32_t r1, r2, r3, expect;

  show_state(1, Yellow, "CPU     : wait Mem");
  osSignalWait(SIG_CPU_GO, osWaitForever);           /* control from Memory  */
  show_state(1, Green, "CPU     : RUN");
  cpuAccessCount++;

  /* Lab 2 conditional-execution loop */
  r1 = 1; r2 = 0;
  while (r1 <= 0x07) {
    if ((r1 - r2) > 0) r1 = r1 + 2;
    else               r2 = r2 + 1;
  }
  condResult = r1;                                   /* expect 9             */

  /* Lab 2 barrel-shifter loop (r3*4 -> LSL #2) */
  r1 = 1; r2 = 0; r3 = 5;
  while (r2 <= 0x18) {
    if ((r1 - r2) > 0) { r1 = r1 + 2; r2 = r1 + (r3 * 4); r3 = r3 / 2; }
    else               { r2 = r2 + 1; }
  }
  barrelResult = r2;                                 /* expect 25            */

  /* internal hardware check: assembly (IT block + LSL operand) vs C */
  expect  = ((condResult > barrelResult) ? (condResult - barrelResult)
                                         : (barrelResult - condResult))
            + (barrelResult << 2);                   /* 16 + 100 = 116       */
  cpuHwOK = (cpu_hw_check(condResult, barrelResult) == expect);
  demo_hold(1);

  osSignalSet(tid_Mem, SIG_MEM_ACK);                 /* signal Memory back   */
  show_state(1, DarkGrey, "CPU     : done");
  osThreadTerminate(osThreadGetId());
}

void AppInterface (void const *argument) {
  show_state(2, Green, "App     : RUN");

  osMutexWait(log_mtx, osWaitForever);               /* exclusive logger     */
  log_append("APP: open file -> ");                  /* partial message      */
  osMutexRelease(log_mtx);
  demo_hold(2);

  show_state(2, Yellow, "App     : wait Dev");
  osSignalSet(tid_Dev, SIG_DEV_GO);                  /* now Device may run   */
  osSignalWait(SIG_APP_ACK, osWaitForever);          /* wait for the ending  */

  appCounter++;
  osDelay(1);                                        /* delay one tick       */
  show_state(2, DarkGrey, "App     : done");
  osThreadTerminate(osThreadGetId());
}

void DeviceManagement (void const *argument) {
  show_state(3, Yellow, "Device  : wait App");
  osSignalWait(SIG_DEV_GO, osWaitForever);           /* App always goes first */
  show_state(3, Green, "Device  : RUN");

  osMutexWait(log_mtx, osWaitForever);
  log_append("DEV: written, closed");                /* ending of the message */
  osMutexRelease(log_mtx);
  demo_hold(3);

  osSignalSet(tid_App, SIG_APP_ACK);                 /* signal the App back  */
  devCounter++;
  osDelay(1);                                        /* "file is closed"     */
  show_state(3, DarkGrey, "Device  : done");
  LCD_Text(7, White, "logger:");
  LCD_Text(8, Cyan, logger);                         /* first 20 characters  */
  LCD_Text(9, Cyan, logger + 20);                    /* rest of the message  */
  osThreadTerminate(osThreadGetId());
}

void UserInterface (void const *argument) {
  show_state(4, Green, "User    : RUN");
  userCount++;                                       /* one more user        */
  demo_hold(4);
  osDelay(1);                                        /* delay one tick       */
  show_state(4, DarkGrey, "User    : done");
  osThreadTerminate(osThreadGetId());
}

/*-------------------------------- main ------------------------------------*/
int main (void) {
  osKernelInitialize();                              /* initialize RTX       */

  log_mtx = osMutexCreate(osMutex(log_mutex));
  Demo_Init("Q2 RR OS threads");

  /* Memory Management is created first, so it is the first thread to run */
  tid_Mem  = osThreadCreate(osThread(MemoryManagement), NULL);
  tid_CPU  = osThreadCreate(osThread(CPUManagement),    NULL);
  tid_App  = osThreadCreate(osThread(AppInterface),     NULL);
  tid_Dev  = osThreadCreate(osThread(DeviceManagement), NULL);
  tid_User = osThreadCreate(osThread(UserInterface),    NULL);

  osKernelStart();                                   /* start thread switching */
  osDelay(osWaitForever);
}
