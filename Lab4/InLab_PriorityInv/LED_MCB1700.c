/*----------------------------------------------------------------------------
 * LED_MCB1700.c  -  Board_LED API for the 8 LEDs of the MCB1700 (Lab 4)
 *   LED 0..7 = P1.28, P1.29, P1.31, P2.2, P2.3, P2.4, P2.5, P2.6
 *---------------------------------------------------------------------------*/
#include "LPC17xx.h"
#include "Board_LED.h"

#define LED_COUNT 8u

static LPC_GPIO_TypeDef * const led_port[LED_COUNT] = {
  LPC_GPIO1, LPC_GPIO1, LPC_GPIO1,
  LPC_GPIO2, LPC_GPIO2, LPC_GPIO2, LPC_GPIO2, LPC_GPIO2
};
static const uint32_t led_pin[LED_COUNT] = { 28, 29, 31, 2, 3, 4, 5, 6 };

int32_t LED_Initialize (void) {
  uint32_t n;
  for (n = 0; n < LED_COUNT; n++) {
    led_port[n]->FIODIR |= (1UL << led_pin[n]);
    led_port[n]->FIOCLR  = (1UL << led_pin[n]);
  }
  return 0;
}

int32_t LED_Uninitialize (void) {
  uint32_t n;
  for (n = 0; n < LED_COUNT; n++) {
    led_port[n]->FIOCLR  = (1UL << led_pin[n]);
    led_port[n]->FIODIR &= ~(1UL << led_pin[n]);
  }
  return 0;
}

int32_t LED_On (uint32_t num) {
  if (num >= LED_COUNT) return -1;
  led_port[num]->FIOSET = (1UL << led_pin[num]);
  return 0;
}

int32_t LED_Off (uint32_t num) {
  if (num >= LED_COUNT) return -1;
  led_port[num]->FIOCLR = (1UL << led_pin[num]);
  return 0;
}

int32_t LED_SetOut (uint32_t val) {
  uint32_t n;
  for (n = 0; n < LED_COUNT; n++) {
    if (val & (1UL << n)) LED_On(n);
    else                  LED_Off(n);
  }
  return 0;
}

uint32_t LED_GetCount (void) { return LED_COUNT; }
