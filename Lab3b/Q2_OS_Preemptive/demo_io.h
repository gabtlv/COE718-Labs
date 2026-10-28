/*----------------------------------------------------------------------------
 * demo_io.h  -  LED / LCD helpers for the DEMO build (COE718 Lab 3b)
 *
 * Theme: AIRPORT CONTROL TOWER. The runway is the CPU: only one plane
 * (thread) can be on it at a time, and the tower (RTX scheduler) always
 * clears the highest-priority flight to land first.
 *
 *   LCD line 0     : title bar
 *   LCD lines 1..5 : flight board, one line per thread
 *   LCD line 6     : tower radio message
 *   below line 6   : holding lane (sky) + runway with the landing plane
 *
 * When DEMO is 0 every helper below is an empty macro, so the ANALYSIS build
 * contains no LED or LCD code (same idea as the LCD macros used in Lab 1).
 *---------------------------------------------------------------------------*/
#ifndef DEMO_IO_H
#define DEMO_IO_H

#include "build_cfg.h"

#define LANE_RUNWAY   0               /* plane on the runway  = running thread  */
#define LANE_HOLD     1               /* plane in the holding lane = pre-empted */

#if DEMO

#include "GLCD.h"

#define Orange        0xFC00          /* extra colour for the flight board      */

/* MCB1700 LEDs, index 0..7 = P1.28, P1.29, P1.31, P2.2, P2.3, P2.4, P2.5, P2.6 */
void Demo_Init (const char *title);                  /* title bar + runway (call after osKernelInitialize) */
void LED_Show  (int idx);                            /* light only LED idx, -1 = all off */
void LED_Set   (unsigned int mask);                  /* bit n lights LED n               */
void LCD_Text  (unsigned int ln, unsigned short color, const char *s); /* 20 chars/line  */
void LCD_Bar   (unsigned int ln, unsigned short color, unsigned int pct);
void Demo_Spin (unsigned int ms);                    /* busy wait (keeps the CPU busy)  */

/* Airport tower */
void Tower_Flight (int row, unsigned short color, char tag, const char *call,
                   int prio, const char *status);    /* "A COE718 P2 LANDING"  */
void Tower_Result (int row, unsigned short color, char tag, const char *call,
                   double value);                    /* "A COE718 = 66306.000" */
void Tower_Radio  (unsigned short color, const char *msg);   /* line 6         */
void Tower_Plane  (int lane, int id, unsigned int pct, unsigned short color);
void Tower_Clear  (int lane);                        /* remove plane from lane  */
void Tower_Text   (unsigned int ln, unsigned short color, const char *s); /* lines 7-9 over the runway */

#else  /* ANALYSIS build: helpers compile to nothing */

#define Demo_Init(title)                     ((void)0)
#define LED_Show(idx)                        ((void)0)
#define LED_Set(mask)                        ((void)0)
#define LCD_Text(ln, color, s)               ((void)0)
#define LCD_Bar(ln, color, pct)              ((void)0)
#define Demo_Spin(ms)                        ((void)0)
#define Tower_Flight(r, c, t, call, p, st)   ((void)0)
#define Tower_Result(r, c, t, call, v)       ((void)0)
#define Tower_Radio(c, msg)                  ((void)0)
#define Tower_Plane(lane, id, pct, c)        ((void)0)
#define Tower_Clear(lane)                    ((void)0)
#define Tower_Text(ln, c, s)                 ((void)0)

#endif /* DEMO */

#endif /* DEMO_IO_H */
