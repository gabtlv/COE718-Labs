/*----------------------------------------------------------------------------
 * COE718 Lab 3b - Question 1: Pre-emptive (priority) scheduling, five tasks
 *
 * Round-Robin is OFF in RTX_Conf_CM.c, so the scheduler always runs the
 * highest-priority READY thread, and threads of equal priority run one after
 * the other (first created first) until they finish.
 *
 *   Task  Function                                 Lab prio  CMSIS priority
 *    A    A = sum(x=0..256)  [x + (x+2)]               2     AboveNormal
 *    B    B = sum(n=1..16)   2^n / n!                  3     Normal
 *    C    C = sum(n=1..16)   (n+1) / n                 1     High
 *    D    D = 1 + 5/1! + 5^2/2! + ... + 5^5/5!         2     AboveNormal
 *    E    E = pi r^2 + 2 pi r^2 + ... + 12 pi r^2      3     Normal
 *
 * Showing pre-emption: Task C (highest priority) starts by waiting for its
 * "request" signal. Task A sends that signal halfway through its own work.
 * At that instant C becomes READY with a higher priority than A, so RTX
 * pre-empts A in the middle of its calculation, runs C to completion, and
 * then resumes A where it stopped. C still finishes first.
 *
 * Expected finish order: C, A, D, B, E   (finishOrder = "CADBE")
 * Expected A_repsAtPreempt = 50 (A was half done when C took the CPU).
 *
 * All five threads are created BEFORE osKernelStart(). If they were created
 * after it, each higher-priority thread would pre-empt main() the moment it
 * was created, so creation order would decide who runs first.
 *
 * Each task has a finite workload. The sum is recomputed WORK_REPEAT times
 * only so that each task is wide enough to read in the Event Viewer; the
 * answer is the same every time. The task stores its result, records its
 * place in finishOrder, and deletes itself.
 *
 * Watch 1: parameters  A_xMax B_nMax C_nMax D_kMax E_kMax E_r
 *          results     resultA resultB resultC resultD resultE
 *          scheduling  finishOrder tasksDone A_repsAtPreempt countIDLE
 *
 * Build switch (build_cfg.h): DEMO 0 = analysis version (simulator, no
 * LED/LCD code). DEMO 1 = demo version (LEDs + LCD show the running thread).
 *---------------------------------------------------------------------------*/
#include "cmsis_os.h"
#include "build_cfg.h"
#include "demo_io.h"

#define WORK_REPEAT   100u           /* recompute count (Event Viewer width)  */
#define PI            3.14159265358979
#define SIG_C_REQUEST 0x01           /* A -> C : "high-priority job arrives"  */
#define DEMO_STEP_MS  15u            /* demo only: 100 steps x 15 ms = 1.5 s  */

/*----------------------- parameters (Watch 1) -----------------------------*/
/* The tasks read their limits from these variables, so the Watch window
   shows exactly what each calculation used. */
volatile uint32_t A_xMax = 256;      /* A: x = 0 .. 256                       */
volatile uint32_t B_nMax = 16;       /* B: n = 1 .. 16                        */
volatile uint32_t C_nMax = 16;       /* C: n = 1 .. 16                        */
volatile uint32_t D_kMax = 5;        /* D: terms 5^k/k!, k = 0 .. 5           */
volatile uint32_t E_kMax = 12;       /* E: k pi r^2, k = 1 .. 12              */
volatile double   E_r    = 2.0;      /* E: radius (handout gives no value)    */

/*------------------------- results (Watch 1) ------------------------------*/
volatile uint32_t resultA = 0;       /* expect 66306                          */
volatile double   resultB = 0;       /* expect 6.389056  (about e^2 - 1)      */
volatile double   resultC = 0;       /* expect 19.380729 (16 + H16)           */
volatile double   resultD = 0;       /* expect 91.416667                      */
volatile double   resultE = 0;       /* expect 980.176908 (78 pi r^2, r = 2)  */

/*------------------------ scheduling (Watch 1) ----------------------------*/
volatile uint32_t tasksDone       = 0;  /* finished tasks (0..5)              */
char              finishOrder[6]  = ""; /* letters in the order tasks finish  */
volatile uint32_t A_repsDone      = 0;  /* A's progress (0..WORK_REPEAT)      */
volatile uint32_t A_repsAtPreempt = 0;  /* A's progress when C took over      */

/*---------------------------- thread objects ------------------------------*/
void TaskA (void const *argument);
void TaskB (void const *argument);
void TaskC (void const *argument);
void TaskD (void const *argument);
void TaskE (void const *argument);

/* lab priority 1 = highest  ->  1 High, 2 AboveNormal, 3 Normal */
osThreadDef(TaskA, osPriorityAboveNormal, 1, 0);
osThreadDef(TaskB, osPriorityNormal,      1, 0);
osThreadDef(TaskC, osPriorityHigh,        1, 0);
osThreadDef(TaskD, osPriorityAboveNormal, 1, 0);
osThreadDef(TaskE, osPriorityNormal,      1, 0);

osThreadId tid_A, tid_B, tid_C, tid_D, tid_E;

/*------------- demo only: airport control tower on the LCD ----------------*/
/* The runway is the CPU and every task is a flight. The tower (scheduler)
   always clears the highest-priority flight to land. Task C is the MAYDAY
   flight: when it calls in, Task A has to go around (it is pre-empted),
   circles in the holding lane while C lands, then lands where it left off.
   LED n = task n is on the runway: 0 A, 1 B, 2 C, 3 D, 4 E. */
#if DEMO
static const char tag[5] = { 'A', 'B', 'C', 'D', 'E' };
static const char *const call[5] = { "COE718", "RTX-4", "MAYDAY", "ARM-M3", "LPC17" };
static const int  lab_prio[5] = { 2, 3, 1, 2, 3 };
static const unsigned short col[5] = { Cyan, Magenta, Red, Yellow, Green };

/* flight board before the kernel starts: everyone is holding */
static void demo_board (void) {
  int i;
  for (i = 0; i < 5; i++)
    Tower_Flight(i + 1, White, tag[i], call[i], lab_prio[i],
                 (i == 2) ? "STANDBY" : "HOLDING");
  Tower_Radio(Cyan, "TWR: 5 flights hold");
}

static void demo_radio_cleared (int idx) {
  char msg[21] = "TWR: X cleared land";
  msg[5] = tag[idx];
  Tower_Radio((idx == 2) ? Red : Cyan, (idx == 2) ? "MAYDAY! C priority 1" : msg);
}

/* Called once per repetition: the task's plane rolls down the runway
   (rep 0..99 -> 1..100 %), its LED is lit, and a busy-wait (not osDelay)
   keeps the CPU so no other task can run. */
static void demo_step (int idx, uint32_t rep) {
  if (rep == 0u) {
    Tower_Flight(idx + 1, col[idx], tag[idx], call[idx], lab_prio[idx], "LANDING");
    demo_radio_cleared(idx);
  }
  Tower_Plane(LANE_RUNWAY, idx, rep + 1u, col[idx]);
  LED_Show(idx);
  Demo_Spin(DEMO_STEP_MS);
}

/* Landed: the result replaces the status on the flight board. */
static void demo_result (int idx, double v) {
  char msg[21] = "TWR: X landed";
  msg[5] = tag[idx];
  Tower_Result(idx + 1, LightGrey, tag[idx], call[idx], v);
  Tower_Clear(LANE_RUNWAY);
  Tower_Radio(Green, msg);
  Demo_Spin(600);                                    /* let people read it   */
}

/* MAYDAY: A is sent around just before C takes the runway. */
static void demo_preempt_A (void) {
  Tower_Flight(1, Orange, tag[0], call[0], lab_prio[0], "GO-ARND");
  Tower_Radio(Orange, "TWR: A go around!");
  Tower_Clear(LANE_RUNWAY);
  Tower_Plane(LANE_HOLD, 0, A_repsDone, col[0]);     /* A circles above      */
  LED_Show(0);
  Demo_Spin(900);
}

/* After C has landed, A gets the runway back where it stopped. */
static void demo_resume_A (void) {
  Tower_Clear(LANE_HOLD);
  Tower_Flight(1, col[0], tag[0], call[0], lab_prio[0], "LANDING");
  Tower_Radio(Cyan, "TWR: A re-cleared");
}

static void demo_all_landed (void) {
  char msg[21] = "ALL LANDED: ";
  int i;
  for (i = 0; i < 5; i++) msg[12 + i] = finishOrder[i];
  msg[17] = 0;
  Tower_Radio(Green, msg);
  LED_Set(0x1Fu);
}
#else
#define demo_board()            ((void)0)
#define demo_step(idx, rep)     ((void)0)
#define demo_result(idx, v)     ((void)0)
#define demo_preempt_A()        ((void)0)
#define demo_resume_A()         ((void)0)
#define demo_all_landed()       ((void)0)
#endif

/* Common end of every task: record the finish order, then delete itself.
   Round-robin is off and a task only pre-empts another before reaching this
   code, so finishOrder is never written by two tasks at the same time. */
static void task_finished (char letter) {
  finishOrder[tasksDone] = letter;
  tasksDone++;
  finishOrder[tasksDone] = 0;
  if (tasksDone == 5u) demo_all_landed();
  osThreadTerminate(osThreadGetId());
}

/*------------------------------ threads -----------------------------------*/
/* Task A (prio 2): sum over x = 0..A_xMax of x + (x + 2).
   Halfway through, it signals Task C, which pre-empts it immediately. */
void TaskA (void const *argument) {
  uint32_t r, x, xMax = A_xMax, sum = 0;
  for (r = 0; r < WORK_REPEAT; r++) {
    sum = 0;
    for (x = 0; x <= xMax; x++) sum += x + (x + 2u);
    resultA    = sum;
    A_repsDone = r + 1u;
    demo_step(0, r);
    if (A_repsDone == WORK_REPEAT / 2u) {
      demo_preempt_A();                    /* demo: A goes around        */
      osSignalSet(tid_C, SIG_C_REQUEST);   /* C is now READY, prio 1 > 2:   */
      demo_resume_A();                     /* we only get here after C ends */
    }
  }
  demo_result(0, (double)resultA);
  task_finished('A');
}

/* Task B (prio 3): sum over n = 1..B_nMax of 2^n / n! */
void TaskB (void const *argument) {
  uint32_t r, n, nMax = B_nMax;
  double pow2, fact, sum = 0;
  for (r = 0; r < WORK_REPEAT; r++) {
    sum = 0; pow2 = 1.0; fact = 1.0;
    for (n = 1; n <= nMax; n++) {
      pow2 *= 2.0;                     /* 2^n                                 */
      fact *= (double)n;               /* n!  (16! > 2^32, so use double)     */
      sum  += pow2 / fact;
    }
    resultB = sum;
    demo_step(1, r);
  }
  demo_result(1, resultB);
  task_finished('B');
}

/* Task C (prio 1, highest): sum over n = 1..C_nMax of (n + 1) / n.
   Waits for its request first, then pre-empts whatever is running. */
void TaskC (void const *argument) {
  uint32_t r, n, nMax = C_nMax;
  double sum = 0;
  osSignalWait(SIG_C_REQUEST, osWaitForever);  /* blocked until A asks     */
  A_repsAtPreempt = A_repsDone;                /* A is frozen here: 50     */
  for (r = 0; r < WORK_REPEAT; r++) {
    sum = 0;
    for (n = 1; n <= nMax; n++) sum += (double)(n + 1u) / (double)n;
    resultC = sum;
    demo_step(2, r);
  }
  demo_result(2, resultC);
  task_finished('C');
}

/* Task D (prio 2): 1 + 5/1! + 5^2/2! + ... + 5^D_kMax / D_kMax! */
void TaskD (void const *argument) {
  uint32_t r, k, kMax = D_kMax;
  double term, sum = 0;
  for (r = 0; r < WORK_REPEAT; r++) {
    sum = 1.0; term = 1.0;
    for (k = 1; k <= kMax; k++) {
      term *= 5.0 / (double)k;         /* 5^k / k! from the previous term     */
      sum  += term;
    }
    resultD = sum;
    demo_step(3, r);
  }
  demo_result(3, resultD);
  task_finished('D');
}

/* Task E (prio 3): pi r^2 + 2 pi r^2 + ... + E_kMax pi r^2 */
void TaskE (void const *argument) {
  uint32_t r, k, kMax = E_kMax;
  double area, sum = 0;
  for (r = 0; r < WORK_REPEAT; r++) {
    area = PI * E_r * E_r;
    sum  = 0;
    for (k = 1; k <= kMax; k++) sum += (double)k * area;
    resultE = sum;
    demo_step(4, r);
  }
  demo_result(4, resultE);
  task_finished('E');
}

/*-------------------------------- main ------------------------------------*/
int main (void) {
  osKernelInitialize();                              /* initialize RTX       */
  Demo_Init(" TOWER: RUNWAY = CPU");
  demo_board();

  /* Create every task before the kernel starts, so the scheduler picks
     purely by priority. C runs first but immediately waits for its request,
     so A (next highest, created first) starts working. */
  tid_A = osThreadCreate(osThread(TaskA), NULL);
  tid_B = osThreadCreate(osThread(TaskB), NULL);
  tid_C = osThreadCreate(osThread(TaskC), NULL);
  tid_D = osThreadCreate(osThread(TaskD), NULL);
  tid_E = osThreadCreate(osThread(TaskE), NULL);

  osKernelStart();                                   /* start scheduling     */
  osDelay(osWaitForever);                            /* main has nothing left */
}
