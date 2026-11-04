/*----------------------------------------------------------------------------
 * Board_LED.h  -  stand-in for the ::Board Support:LED component (Lab 4)
 *
 * The D2L example code includes "Board_LED.h". The real header comes from
 * the MDK-Middleware Board Support pack, which the lab PCs may not have, so
 * this project uses this plain header + LED_MCB1700.c instead. Same API.
 *---------------------------------------------------------------------------*/
#ifndef BOARD_LED_H
#define BOARD_LED_H

#include <stdint.h>

int32_t  LED_Initialize   (void);
int32_t  LED_Uninitialize (void);
int32_t  LED_On           (uint32_t num);
int32_t  LED_Off          (uint32_t num);
int32_t  LED_SetOut       (uint32_t val);
uint32_t LED_GetCount     (void);

#endif /* BOARD_LED_H */
