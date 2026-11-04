/*----------------------------------------------------------------------------
 * COE718 Lab 4 - Part 2: Mars Pathfinder priority inversion, problem + fix
 *
 *   Task  Role              CMSIS priority      Released   Work (CPU time)
 *    A    high (bus mgmt)   AboveNormal          60 ms      R1 for 10 ms, then 10 ms
 *    B    medium (comms)    Normal               50 ms      30 ms, never uses R1
 *    C    low (weather)     BelowNormal           0 ms      20 ms, R1 for 50 ms, then 10 ms
 *
 * R1 (the shared bus) is a plain Boolean, r1_inUse, NOT an RTX mutex: an
 * RTX mutex does priority inheritance by itself and would hide the problem.
 *
 * SOLUTION 0 - THE PROBLEM (handout Figure 3)
 *     0 -  20 ms  C executing
 *    20 -  50 ms  C executing in R1 (30 of its 50 ms done)
 *    50 -  60 ms  B pre-empts C, C still holds R1
 *    60 ms  ...   A pre-empts B and needs R1. R1 is in use, so A polls the
 *                 flag. A is the highest-priority ready task, so C never gets
 *                 the CPU back to free R1, and B never finishes either:
 *                 A is blocked by the LOW-priority task forever.
 *
 * SOLUTION 1 - THE FIX: priority inheritance, done by hand
 *   When A finds R1 in use it lends its own priority to the owner (C) and
 *   sleeps (osSignalWait) until R1 is free. C now outranks B, finishes its
 *   critical section, frees R1, wakes A, and drops back to its own priority,
 *   at which point A pre-empts it.
 *     0 -  20 ms  C          50 -  60 ms  B             80 - 100 ms  A (R1, then work)
 *    20 -  50 ms  C (R1)     60 -  80 ms  C (R1, at A's priority)
 *                                                       100 - 120 ms  B (remaining 20)
 *                                                       120 - 130 ms  C (remaining 10)
 *
 * Watch 1: traceWho / traceT_ms (who got the CPU, when), finish_ms,
 *          r1_inUse, r1_owner, A_polls (problem only), C_r1Done_ms
 *   Problem : traceWho = "CBA",      A_polls keeps growing, finish_ms all 0
 *   Solution: traceWho = "CBACABC",  finish_ms A 100, B 120, C 130
 *
 * Note: RTX itself never lets this happen with an os_mut mutex; the
 * Boolean flag is what exposes the inversion.
 *---------------------------------------------------------------------------*/
#include "cmsis_os.h"

/*>>>>>>>>>>>>>>>>>>>>>  0 = show the problem, 1 = solution  <<<<<<<<<<<<<<<<*/
#define SOLUTION   1

#define SIG_GO       0x01             /* timer -> task: you are released      */
#define SIG_R1_FREE  0x02             /* C -> A: R1 has been freed (solution) */

enum { TA = 0, TB = 1, TC = 2, NTASK = 3 };
const char taskName[NTASK] = { 'A', 'B', 'C' };

/*------------------------------ Watch 1 -----------------------------------*/
volatile uint8_t  r1_inUse = 0;       /* the shared resource R1 (Boolean)     */
volatile char     r1_owner = '-';
volatile uint32_t A_polls  = 0;       /* times A found R1 busy (problem)      */
volatile uint32_t C_r1Done_ms = 0;    /* when C freed R1                      */
volatile uint32_t finish_ms[NTASK];   /* when each task finished (0 = never)  */
volatile char     running = '-';

#define TRACE_N 12
volatile char     traceWho[TRACE_N + 1];
volatile uint32_t traceT_ms[TRACE_N];
volatile uint32_t traceN;

/*-------------------------------- state -----------------------------------*/
extern uint32_t os_time;              /* RTX tick counter (1 tick = 1 ms)     */
static uint32_t t0_ms;
static uint32_t now_ms (void) { return os_time - t0_ms; }

/*------------------------------- threads ----------------------------------*/
void TaskA (void const *argument);
void TaskB (void const *argument);
void TaskC (void const *argument);

osThreadDef(TaskA, osPriorityAboveNormal, 1, 0);   /* high   */
osThreadDef(TaskB, osPriorityNormal,      1, 0);   /* medium */
osThreadDef(TaskC, osPriorityBelowNormal, 1, 0);   /* low    */

osThreadId tid[NTASK];
const osPriority basePrio[NTASK] = { osPriorityAboveNormal, osPriorityNormal, osPriorityBelowNormal };

/*----------------- virtual timers: release B at 50, A at 60 ---------------*/
void release (void const *arg) {
  osSignalSet(tid[(uint32_t)arg], SIG_GO);
}
osTimerDef(timerA, release);
osTimerDef(timerB, release);

/*------------------------- CPU-time busy wait -----------------------------*/
static void got_cpu (int i) {
  running = taskName[i];
  if (traceN < TRACE_N) {
    traceT_ms[traceN] = now_ms();
    traceWho[traceN]  = taskName[i];
    traceN++;
  }
}

/* Busy-wait for `ms` of CPU time of task i. Time while another task had the
   CPU (running changed) is not counted, so a pre-empted task resumes its
   work where it stopped. osKernelSysTick() can step back a few counts at a
   tick boundary (a huge unsigned difference), so only small forward steps
   (under 1 ms) are counted. */
static void work (int i, uint32_t ms) {
  uint32_t need = (uint32_t)osKernelSysTickMicroSec(ms * 1000u);
  uint32_t maxStep = (uint32_t)osKernelSysTickMicroSec(1000u);
  uint32_t used = 0, now, d, last = osKernelSysTick();
  while (used < need) {
    now = osKernelSysTick();
    d   = now - last;
    if (running != taskName[i]) got_cpu(i);
    else if (d < maxStep)       used += d;
    last = now;
  }
}

/*---------------------------- resource R1 ---------------------------------*/
#if SOLUTION
static volatile int r1_waiter = -1;   /* task sleeping until R1 is free       */
#endif

static void R1_acquire (int me) {
  /* Only one CPU and A is the highest-priority task, so nothing can run
     between the test and the set below. */
		while (r1_inUse) {
#if SOLUTION
    /*** SOLUTION: priority inheritance. Lend my priority to the owner and
         sleep until it frees R1, so B (medium) can no longer pre-empt it. */
    int owner = (r1_owner == 'A') ? TA : (r1_owner == 'B') ? TB : TC;
    r1_waiter = me;
    osThreadSetPriority(tid[owner], basePrio[me]);
    osSignalWait(SIG_R1_FREE, osWaitForever);
#else
    /*** PROBLEM: poll the flag. C holds R1 but can never run again. */
    A_polls++;
#endif
  }
  r1_inUse = 1;
  r1_owner = taskName[me];
}

static void R1_release (int me) {
  r1_inUse = 0;
  r1_owner = '-';
#if SOLUTION
  /*** SOLUTION: wake the waiter first (it has the same priority as me now,
       and round-robin is off, so it cannot run yet), THEN drop back to my
       own priority. Dropping first would let B pre-empt me before A is
       woken, which is the inversion all over again. */
  if (r1_waiter >= 0) {
    int w = r1_waiter;
    r1_waiter = -1;
    osSignalSet(tid[w], SIG_R1_FREE);
  }
  osThreadSetPriority(tid[me], basePrio[me]);   /* A pre-empts me here */
#else
  (void)me;
#endif
}

/*------------------------------- tasks ------------------------------------*/
/* C (low): weather task, starts at t = 0 and holds R1 for 50 ms */
void TaskC (void const *argument) {
  got_cpu(TC);
  work(TC, 20);                   /* executing                              */
  R1_acquire(TC);
  work(TC, 50);                   /* executing in R1 (B pre-empts at 50 ms) */
  C_r1Done_ms = now_ms();
  R1_release(TC);
  work(TC, 10);
  finish_ms[TC] = now_ms();
  running = '-';
  osThreadTerminate(osThreadGetId());
}

/* B (medium): communications task, released at 50 ms, never uses R1 */
void TaskB (void const *argument) {
  osSignalWait(SIG_GO, osWaitForever);
  got_cpu(TB);
  work(TB, 30);
  finish_ms[TB] = now_ms();
  running = '-';
  osThreadTerminate(osThreadGetId());
}

/* A (high): bus management task, released at 60 ms, needs R1 */
void TaskA (void const *argument) {
  osSignalWait(SIG_GO, osWaitForever);
  got_cpu(TA);
  R1_acquire(TA);                 /* problem: stuck here for ever           */
  got_cpu(TA);                    /* (solution: A owns R1 from here)        */
  work(TA, 10);                   /* use R1                                 */
  R1_release(TA);
  work(TA, 10);
  finish_ms[TA] = now_ms();
  running = '-';
  osThreadTerminate(osThreadGetId());
}

/*--------------------------------- main -----------------------------------*/
int main (void) {
  osTimerId tmrA, tmrB;
  osKernelInitialize();
  osThreadSetPriority(osThreadGetId(), osPriorityRealtime);  /* set up first */

  tid[TC] = osThreadCreate(osThread(TaskC), NULL);
  tid[TB] = osThreadCreate(osThread(TaskB), NULL);
  tid[TA] = osThreadCreate(osThread(TaskA), NULL);
  tmrB = osTimerCreate(osTimer(timerB), osTimerOnce, (void *)TB);
  tmrA = osTimerCreate(osTimer(timerA), osTimerOnce, (void *)TA);

  osKernelStart();
  t0_ms = os_time;                /* t = 0: C starts now                    */
  osTimerStart(tmrB, 50);         /* B released at 50 ms                    */
  osTimerStart(tmrA, 60);         /* A released at 60 ms                    */

  osThreadTerminate(osThreadGetId());
  for (;;) { }
}
