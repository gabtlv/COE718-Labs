/*----------------------------------------------------------------------------
 * demo_io.h  -  LED / LCD helpers for the DEMO build (COE718 Lab 4, Part 1)
 *
 * Theme: RMS SCOPE. The LCD works like an oscilloscope with two traces:
 *   PLAN : the RMS timeline calculated on the board itself (a small RMS
 *          simulation of Table 1 run once at start-up)
 *   LIVE : what RTX actually runs, drawn column by column by the running
 *          task while a white cursor sweeps across the 4 s hyperperiod.
 * If the scheduler is right, the LIVE trace is a copy of the PLAN trace.
 *
 * LEDs: LED7 = A running, LED6 = B, LED5 = C, LED0..4 = progress of the
 * running job (a pre-empted job's bar picks up where it stopped).
 *
 * When DEMO is 0 every helper is an empty macro, so the analysis build has
 * no LED or LCD code.
 *---------------------------------------------------------------------------*/
#ifndef DEMO_IO_H
#define DEMO_IO_H

#include <stdint.h>
#include "build_cfg.h"

#if DEMO

void Demo_Init  (const uint32_t T[3], const uint32_t C[3], uint32_t unit_us);
void Demo_Run   (int i, uint32_t job, int resumed);        /* task i got the CPU    */
void Demo_Tick  (int i, uint32_t used, uint32_t need, uint32_t t_ms); /* every pass of delay() */
void Demo_Stats (const volatile uint32_t jobs[3], const volatile uint32_t miss[3],
                 const volatile uint32_t resp_ms[3]);

#else  /* analysis build: helpers compile to nothing */

#define Demo_Init(T, C, u)                ((void)0)
#define Demo_Run(i, job, resumed)         ((void)0)
#define Demo_Tick(i, used, need, t)       ((void)0)
#define Demo_Stats(jobs, miss, resp)      ((void)0)

#endif /* DEMO */

#endif /* DEMO_IO_H */
