/*----------------------------------------------------------------------------
 * demo_io.h  -  LED / LCD helpers for the DEMO build (COE718 Lab 3a)
 *
 * When DEMO is 0 every helper below is an empty macro, so the ANALYSIS build
 * contains no LED or LCD code (same idea as the LCD macros used in Lab 1).
 *---------------------------------------------------------------------------*/
#ifndef DEMO_IO_H
#define DEMO_IO_H

#include "build_cfg.h"

#if DEMO

#include "GLCD.h"

/* MCB1700 LEDs, index 0..7 = P1.28, P1.29, P1.31, P2.2, P2.3, P2.4, P2.5, P2.6 */
void Demo_Init (const char *title);                  /* call after osKernelInitialize() */
void LED_Show  (int idx);                            /* light only LED idx, -1 = all off */
void LED_Set   (unsigned int mask);                  /* bit n lights LED n               */
void LCD_Text  (unsigned int ln, unsigned short color, const char *s); /* 20 chars/line  */
void LCD_Bar   (unsigned int ln, unsigned short color, unsigned int pct);
void Demo_Spin (unsigned int ms);                    /* busy wait (keeps the CPU busy)  */

#else  /* ANALYSIS build: helpers compile to nothing */

#define Demo_Init(title)          ((void)0)
#define LED_Show(idx)             ((void)0)
#define LED_Set(mask)             ((void)0)
#define LCD_Text(ln, color, s)    ((void)0)
#define LCD_Bar(ln, color, pct)   ((void)0)
#define Demo_Spin(ms)             ((void)0)

#endif /* DEMO */

#endif /* DEMO_IO_H */
