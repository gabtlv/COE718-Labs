/*----------------------------------------------------------------------------
 * COE718 Lab 3b - Question 2: Pre-emptive (priority) scheduling of an OS problem
 *
 * Round-Robin is OFF in RTX_Conf_CM.c. The scheduler always runs the highest-
 * priority READY thread; equal priorities run in creation order until they
 * block or finish. Each thread has its own global variable and a finite
 * workload, and deletes itself when done.
 *
 *   Lab prio 1 -> osPriorityHigh         Memory Management, CPU Management
 *   Lab prio 2 -> osPriorityAboveNormal  Application Interface, Device Mgmt
 *   Lab prio 4 -> osPriorityBelowNormal  User Interface
 *
 * Expected order: Memory -> CPU -> Memory (osDelay) -> App -> Device -> App
 * (osDelay) -> User (osDelay) -> idle. One tick later the delayed threads
 * wake up and pre-empt idle in priority order: Memory, then Device and App,
 * then User, and each deletes itself.
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
 * main() starts thread execution with Memory Management: it is created first
 * and has the highest priority, and every thread is created before
 * osKernelStart() so nothing runs until the kernel starts.
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

osThreadDef(MemoryManagement, osPriorityHigh,        1, 0);  /* prio 1 */
osThreadDef(CPUManagement,    osPriorityHigh,        1, 0);  /* prio 1 */
osThreadDef(AppInterface,     osPriorityAboveNormal, 1, 0);  /* prio 2 */
osThreadDef(DeviceManagement, osPriorityAboveNormal, 1, 0);  /* prio 2 */
osThreadDef(UserInterface,    osPriorityBelowNormal, 1, 0);  /* prio 4 */

osThreadId tid_Mem, tid_CPU, tid_App, tid_Dev, tid_User;

/*------------- demo only: airport control tower on the LCD ----------------*/
/* The runway is the CPU and every OS thread is a flight. The tower
   (scheduler) clears the highest-priority flight first:
     LANDING  = running        CIRCLING = waiting for a signal
     TAXI     = osDelay(1)     AT GATE  = deleted itself
   LED n = thread n is on the runway: 0 Memory, 1 CPU, 2 App, 3 Device, 4 User */
#if DEMO
static const char tag[5] = { 'M', 'C', 'A', 'D', 'U' };
static const char *const call[5] = { "MEMORY", "CPU", "APP", "DEVICE", "USER" };
static const int  lab_prio[5] = { 1, 1, 2, 2, 4 };
static const unsigned short col[5] = { Cyan, Magenta, Yellow, Green, Orange };
static volatile uint32_t at_gate = 0;

static void demo_board (void) {                      /* before osKernelStart */
  int i;
  for (i = 0; i < 5; i++)
    Tower_Flight(i + 1, White, tag[i], call[i], lab_prio[i], "HOLDING");
  Tower_Radio(Cyan, "TWR: 5 flights hold");
}

static void demo_state (int idx, const char *status) {
  unsigned short c = White;                          /* CIRCLING             */
  if (status[0] == 'L') c = col[idx];                /* LANDING              */
  if (status[0] == 'T') c = LightGrey;               /* TAXI                 */
  Tower_Flight(idx + 1, c, tag[idx], call[idx], lab_prio[idx], status);
}

/* The thread's plane lands (busy-wait, not osDelay, so it keeps the CPU).
   A higher-priority flight whose delay ends now pre-empts it mid-landing. */
static void demo_hold (int idx) {
  uint32_t t;
  char msg[21] = "TWR: X cleared land";
  demo_state(idx, "LANDING");
  if ((idx % 2) == 0) {              /* CPU and Device keep the hand-off call */
    msg[5] = tag[idx];
    Tower_Radio(col[idx], msg);
  }
  for (t = 0; t < 100u; t++) {
    Tower_Plane(LANE_RUNWAY, idx, t + 1u, col[idx]);
    LED_Show(idx);
    Demo_Spin(DEMO_HOLD_MS / 100u);
  }
  Tower_Clear(LANE_RUNWAY);
}

/* Deleted: the flight is at the gate. The last one shows the logger. */
static void demo_gate (int idx) {
  uint32_t n, i;
  char part[18];
  demo_state(idx, "AT GATE");
  __disable_irq();                                   /* count without races  */
  n = ++at_gate;
  __enable_irq();
  if (n == 5u) {
    Tower_Text(7, White, "logger:");
    for (i = 0; i < 17u; i++) part[i] = logger[i];   /* "APP: open file ->"  */
    part[17] = 0;
    Tower_Text(8, Yellow, part);
    Tower_Text(9, Green, logger + 18);               /* "DEV: written, closed" */
    Tower_Radio(Green, "ALL FLIGHTS AT GATE");
    LED_Set(0x1Fu);
  }
}
#define demo_radio(c, msg)   Tower_Radio(c, msg)
#else
#define demo_board()                  ((void)0)
#define demo_state(idx, status)       ((void)0)
#define demo_hold(idx)                ((void)0)
#define demo_gate(idx)                ((void)0)
#define demo_radio(c, msg)            ((void)0)
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

  demo_state(0, "CIRCLING");
  demo_radio(Cyan, "MEM>CPU: your turn");
  osSignalSet(tid_CPU, SIG_CPU_GO);                  /* pass control to CPU  */
  osSignalWait(SIG_MEM_ACK, osWaitForever);          /* wait for CPU's reply */

  demo_state(0, "TAXI");
  osDelay(1);                                        /* delay one tick       */
  demo_gate(0);
  osThreadTerminate(osThreadGetId());
}

void CPUManagement (void const *argument) {
  int32_t r1, r2, r3, expect;

  osSignalWait(SIG_CPU_GO, osWaitForever);           /* control from Memory  */
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

  demo_radio(Cyan, "CPU>MEM: HW check OK");
  osSignalSet(tid_Mem, SIG_MEM_ACK);                 /* signal Memory back   */
  demo_gate(1);
  osThreadTerminate(osThreadGetId());
}

void AppInterface (void const *argument) {
  osMutexWait(log_mtx, osWaitForever);               /* exclusive logger     */
  log_append("APP: open file -> ");                  /* partial message      */
  osMutexRelease(log_mtx);
  demo_hold(2);

  demo_state(2, "CIRCLING");
  demo_radio(Cyan, "APP>DEV: end the log");
  osSignalSet(tid_Dev, SIG_DEV_GO);                  /* now Device may run   */
  osSignalWait(SIG_APP_ACK, osWaitForever);          /* wait for the ending  */

  appCounter++;
  demo_state(2, "TAXI");
  osDelay(1);                                        /* delay one tick       */
  demo_gate(2);
  osThreadTerminate(osThreadGetId());
}

void DeviceManagement (void const *argument) {
  osSignalWait(SIG_DEV_GO, osWaitForever);           /* App always goes first */

  osMutexWait(log_mtx, osWaitForever);
  log_append("DEV: written, closed");                /* ending of the message */
  osMutexRelease(log_mtx);
  demo_hold(3);

  demo_radio(Cyan, "DEV>APP: log closed");
  osSignalSet(tid_App, SIG_APP_ACK);                 /* signal the App back  */
  devCounter++;
  demo_state(3, "TAXI");
  osDelay(1);                                        /* "file is closed"     */
  demo_gate(3);
  osThreadTerminate(osThreadGetId());
}

void UserInterface (void const *argument) {
  userCount++;                                       /* one more user        */
  demo_hold(4);
  demo_state(4, "TAXI");
  osDelay(1);                                        /* delay one tick       */
  demo_gate(4);
  osThreadTerminate(osThreadGetId());
}

/*-------------------------------- main ------------------------------------*/
int main (void) {
  osKernelInitialize();                              /* initialize RTX       */

  log_mtx = osMutexCreate(osMutex(log_mutex));
  Demo_Init(" TOWER: OS AIRSPACE");
  demo_board();

  /* Memory Management is created first, so it is the first thread to run */
  tid_Mem  = osThreadCreate(osThread(MemoryManagement), NULL);
  tid_CPU  = osThreadCreate(osThread(CPUManagement),    NULL);
  tid_App  = osThreadCreate(osThread(AppInterface),     NULL);
  tid_Dev  = osThreadCreate(osThread(DeviceManagement), NULL);
  tid_User = osThreadCreate(osThread(UserInterface),    NULL);

  osKernelStart();                                   /* start thread switching */
  osDelay(osWaitForever);
}
