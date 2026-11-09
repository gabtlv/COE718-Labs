/*----------------------------------------------------------------------------
 * input.h - debounced joystick + INT0 button events
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Event codes returned by input_get() */
#define EV_NONE      0
#define EV_UP        1
#define EV_DOWN      2
#define EV_LEFT      3
#define EV_RIGHT     4
#define EV_SELECT    5    /* short press of the joystick centre (on release)  */
#define EV_BACK      6    /* joystick centre held for BACK_HOLD_MS            */
#define EV_INT0      7    /* INT0 push button pressed                         */

#define EV_REPEAT    0x80 /* OR-ed onto a direction event when it auto-repeats */
#define EV_CODE(e)   ((e) & 0x7F)

#define BACK_HOLD_MS 800  /* how long SELECT must be held to go back           */

void    input_init(void);        /* configure pins                             */
void    input_poll(void);        /* call every 10 ms (from SysTick)            */
uint8_t input_get(void);         /* next event or EV_NONE                      */
void    input_flush(void);       /* drop any queued events                     */
uint8_t input_select_down(void); /* 1 while the joystick centre is held        */

#endif
