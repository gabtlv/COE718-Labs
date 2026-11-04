/*----------------------------------------------------------------------------
 * COE718 Lab 4 - Part 1: Rate Monotonic Scheduling (RMS), 3-process set
 *
 *   Process  Period T  Computation C  Lab prio  CMSIS priority
 *      A      40000       20000          3      BelowNormal
 *      B      40000       10000          2      Normal
 *      C      20000        5000          1      AboveNormal
 *
 *   Time scale: 1 table unit = 0.1 ms (UNIT_US), so
 *      A runs 2.0 s every 4 s,  B runs 1.0 s every 4 s,  C runs 0.5 s every 2 s
 *
 *   U = 20000/40000 + 10000/40000 + 5000/20000 = 0.50 + 0.25 + 0.25 = 1.00
 *   This is above the RMS bound n(2^(1/n) - 1) = 0.780 for n = 3, but the
 *   periods are harmonic (20000 divides 40000), and for harmonic periods RMS
 *   meets every deadline as long as U <= 1.
 *
 *   Expected timeline, one hyperperiod (0 .. 4000 ms), repeating forever:
 *        0 -  500 ms  C   all three released at 0, C has the highest priority
 *      500 - 1500 ms  B
 *     1500 - 2000 ms  A   A has done 500 of its 2000 ms
 *     2000 - 2500 ms  C   2nd release of C pre-empts A
 *     2500 - 4000 ms  A   A finishes the remaining 1500 ms at its deadline
 *
 * How it works
 *   - Three periodic virtual timers release the jobs: timer A and timer B
 *     every 4000 ms, timer C every 2000 ms. The callback runs in the RTX
 *     timer thread (OS_TIMERPRIO = High, above all three tasks) and only
 *     sends SIG_RELEASE to its task (inter-thread communication).
 *   - Each task waits in osSignalWait(SIG_RELEASE, osWaitForever) and then
 *     runs one job with delay(), which busy-waits for C of CPU time. Only
 *     time while the task owns the CPU is counted, so a pre-empted job
 *     resumes where it left off, like a real computation.
 *   - Round-robin is OFF, so RTX always runs the highest-priority ready task.
 *   - main() releases all three tasks at t = 0 and starts the timers at the
 *     same instant, then deletes itself.
 *
 * Watch 1: jobs, misses, worstResp_ms, running, traceWho, traceT_ms
 *   traceWho should read "CBACACBACACBACAC" (who got the CPU, in order) and
 *   traceT_ms 0, 500, 1500, 2000, 2500, 4000, 4500, ...
 *
 * Build switch (build_cfg.h): DEMO 0 = analysis version for the simulator
 * (no LED/LCD code). DEMO 1 = board demo (LEDs + LCD "RMS scope").
 *---------------------------------------------------------------------------*/
#include "cmsis_os.h"
#include "build_cfg.h"
#include "demo_io.h"

#define UNIT_US      100u           /* 1 table unit = 0.1 ms                  */
#define SLACK_US     100u           /* each job is 0.1 ms shorter, see delay() */
#define SIG_RELEASE  0x01

enum { TA = 0, TB = 1, TC = 2, NTASK = 3 };

/*------------------------- Table 1 (table units) --------------------------*/
const uint32_t T_units[NTASK] = { 40000u, 40000u, 20000u };   /* period      */
const uint32_t C_units[NTASK] = { 20000u, 10000u,  5000u };   /* computation */
const char     taskName[NTASK] = { 'A', 'B', 'C' };

/*------------------------------ Watch 1 -----------------------------------*/
volatile uint32_t jobs[NTASK];          /* jobs finished                       */
volatile uint32_t misses[NTASK];        /* released while previous job unfinished */
volatile uint32_t lastResp_ms[NTASK];   /* finish - release of the last job    */
volatile uint32_t worstResp_ms[NTASK];  /* must stay <= period: 4000/4000/2000 */
volatile char     running = '-';        /* task that has the CPU right now     */

#define TRACE_N 16
volatile char     traceWho[TRACE_N + 1];/* task letter each time a task gets the CPU */
volatile uint32_t traceT_ms[TRACE_N];   /* ... and when (ms after t = 0)        */
volatile uint32_t traceN;

/*-------------------------------- state -----------------------------------*/
extern uint32_t os_time;                /* RTX tick counter (1 tick = 1 ms)     */
static uint32_t t0_ms;                  /* os_time at t = 0                     */
static volatile uint32_t release_ms[NTASK];
static volatile uint8_t  busy[NTASK];   /* job in progress                      */
static volatile uint8_t  pending[NTASK];/* released, job not started yet        */

static uint32_t now_ms (void) { return os_time - t0_ms; }

/*------------------------------- threads ----------------------------------*/
void TaskA (void const *argument);
void TaskB (void const *argument);
void TaskC (void const *argument);

/* Rate monotonic: shorter period = higher priority (lab 1 = highest).
   A and B have the same period, the table gives B the higher one. */
osThreadDef(TaskA, osPriorityBelowNormal, 1, 0);   /* lab priority 3 */
osThreadDef(TaskB, osPriorityNormal,      1, 0);   /* lab priority 2 */
osThreadDef(TaskC, osPriorityAboveNormal, 1, 0);   /* lab priority 1 */

osThreadId tid[NTASK];

/*---------------------------- virtual timers ------------------------------*/
/* One callback for all three timers; the argument is the task index.
   Runs in the RTX timer thread, which has a higher priority than every task,
   so all jobs released at the same instant are released before any runs. */
void release (void const *arg) {
  uint32_t i = (uint32_t)arg;
  if (busy[i] || pending[i]) misses[i]++;          /* deadline missed         */
  release_ms[i] = now_ms();
  pending[i] = 1;
  osSignalSet(tid[i], SIG_RELEASE);
}
osTimerDef(timerA, release);
osTimerDef(timerB, release);
osTimerDef(timerC, release);
osTimerId tmr[NTASK];

/*------------------------------- delay() ----------------------------------*/
/* Task i got the CPU: at the start of a job or when it resumes after being
   pre-empted. */
static void got_cpu (int i, int resumed) {
  running = taskName[i];
  if (traceN < TRACE_N) {
    traceT_ms[traceN] = now_ms();
    traceWho[traceN]  = taskName[i];
    traceN++;
  }
  Demo_Run(i, jobs[i] + 1u, resumed);
}

/* Custom delay(): busy-wait for `us` microseconds of CPU time of task i.
   Every pass adds the time since the previous pass (measured with the
   100 MHz osKernelSysTick counter), unless another task ran in between
   (running is no longer this task); that gap was a pre-emption and is not
   counted. Interrupts and the timer callbacks take only microseconds and
   are counted, as they would be on a real CPU.
   osKernelSysTick() can step back a few counts at a tick boundary; as an
   unsigned difference that looks like ~43 s, so any step that is not a
   small forward step (MAX_STEP) is ignored. */
#define MAX_STEP_US  100000u        /* 100 ms: longer than any LCD update     */

static void delay (int i, uint32_t us) {
  uint32_t need = (uint32_t)osKernelSysTickMicroSec(us);
  uint32_t maxStep = (uint32_t)osKernelSysTickMicroSec(MAX_STEP_US);
  uint32_t used = 0, now, last, d;

  got_cpu(i, 0);
  last = osKernelSysTick();
  while (used < need) {
    now = osKernelSysTick();
    d   = now - last;
    if (running != taskName[i]) got_cpu(i, 1);     /* resumed after pre-emption */
    else if (d < maxStep)       used += d;         /* my own CPU time           */
    last = now;
    Demo_Tick(i, used, need, now_ms());
  }
}

/* One job of task i. U = 1.00 leaves no idle time at all, so the few
   microseconds RTX spends switching tasks would push A just past its
   deadline at t = 4000 ms. Each job therefore runs C - SLACK_US (0.1 ms,
   one table unit) to leave room for that overhead. */
static void run_job (int i) {
  uint32_t r;
  pending[i] = 0;
  busy[i]    = 1;
  Demo_Stats(jobs, misses, worstResp_ms);
  delay(i, C_units[i] * UNIT_US - SLACK_US);

  r = now_ms() - release_ms[i];
  lastResp_ms[i] = r;
  if (r > worstResp_ms[i]) worstResp_ms[i] = r;
  jobs[i]++;
  busy[i] = 0;
  running = '-';
}

/* Periodic task: wait to be released, run one job, repeat. */
void TaskA (void const *argument) {
  for (;;) { osSignalWait(SIG_RELEASE, osWaitForever); run_job(TA); }
}
void TaskB (void const *argument) {
  for (;;) { osSignalWait(SIG_RELEASE, osWaitForever); run_job(TB); }
}
void TaskC (void const *argument) {
  for (;;) { osSignalWait(SIG_RELEASE, osWaitForever); run_job(TC); }
}

/*--------------------------------- main -----------------------------------*/
int main (void) {
  int i;
  osKernelInitialize();
  /* main outranks everything (timer thread included) while it sets up, so
     nothing runs until every task is released and every timer started. */
  osThreadSetPriority(osThreadGetId(), osPriorityRealtime);
  Demo_Init(T_units, C_units, UNIT_US);

  tid[TA] = osThreadCreate(osThread(TaskA), NULL);
  tid[TB] = osThreadCreate(osThread(TaskB), NULL);
  tid[TC] = osThreadCreate(osThread(TaskC), NULL);

  tmr[TA] = osTimerCreate(osTimer(timerA), osTimerPeriodic, (void *)TA);
  tmr[TB] = osTimerCreate(osTimer(timerB), osTimerPeriodic, (void *)TB);
  tmr[TC] = osTimerCreate(osTimer(timerC), osTimerPeriodic, (void *)TC);

  osKernelStart();

  /* t = 0: release every task and start the period timers together */
  t0_ms = os_time;
  for (i = 0; i < NTASK; i++) {
    release_ms[i] = 0;
    pending[i]    = 1;
    osSignalSet(tid[i], SIG_RELEASE);
    osTimerStart(tmr[i], T_units[i] * UNIT_US / 1000u);   /* period in ms */
  }

  osThreadTerminate(osThreadGetId());       /* main is no longer needed     */
  for (;;) { }
}
