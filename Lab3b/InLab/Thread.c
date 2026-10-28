#include "cmsis_os.h"                                           // CMSIS RTOS header file
/*----------------------------------------------------------------------------
 * COE718 Lab 3b - in-lab exercises (Round-Robin is OFF in RTX_Conf_CM.c)
 *
 * Set EXERCISE to 1, 2 or 3, then Project -> Rebuild all target files.
 *   1 : Thread1 = AboveNormal, both threads loop forever  -> only Thread1 runs
 *   2 : both Normal, each thread calls osThreadYield()    -> ~2.5 us ping-pong
 *   3 : both Normal, Thread1 osDelay(2), Thread2 osDelay(1) -> CPU mostly idle
 *
 * Watch 1: counta countb countIDLE
 *---------------------------------------------------------------------------*/
#define EXERCISE   1

unsigned int counta=0;
unsigned int countb=0;

void Thread1 (void const *argument); // thread function
void Thread2 (void const *argument); // thread function

osThreadId tid_Thread; // thread id
#if (EXERCISE == 1)
osThreadDef (Thread1, osPriorityAboveNormal, 1, 0);              // thread object
#else
osThreadDef (Thread1, osPriorityNormal, 1, 0);                   // thread object
#endif
osThreadId tid2_Thread; // thread id
osThreadDef (Thread2, osPriorityNormal, 1, 0);                   // thread object


int Init_Thread (void) {
  tid_Thread  = osThreadCreate (osThread(Thread1), NULL);
  tid2_Thread = osThreadCreate (osThread(Thread2), NULL);

  if(!tid_Thread) return(-1);
  return(0);
}

void Thread2 (void const *argument) {
  for(;;) {
    countb++;
#if (EXERCISE == 2)
    osThreadYield();                 // hand the CPU to the next ready thread
#elif (EXERCISE == 3)
    osDelay(1);                      // block for 1 tick
#endif
  }
}

void Thread1 (void const *argument) {
  for(;;) {
    counta++;
#if (EXERCISE == 2)
    osThreadYield();
#elif (EXERCISE == 3)
    osDelay(2);                      // block for 2 ticks
#endif
  }
}
