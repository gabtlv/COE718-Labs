/*----------------------------------------------------------------------------
 * demo_io.c  -  LED / LCD helpers for the DEMO build (COE718 Lab 3a)
 *
 * Compiled only when DEMO = 1 (build_cfg.h). The LCD is shared by several
 * round-robin threads, so every LCD access is protected by a mutex: without
 * it a time-slice switch in the middle of an SPI transfer garbles the screen.
 *---------------------------------------------------------------------------*/
#include "build_cfg.h"

#if DEMO

#include "LPC17xx.h"
#include "cmsis_os.h"
#include "demo_io.h"

#define P1_LEDS   ((1UL << 28) | (1UL << 29) | (1UL << 31))
#define P2_LEDS   ((1UL <<  2) | (1UL <<  3) | (1UL <<  4) | (1UL << 5) | (1UL << 6))

static const uint32_t led_bit[8] = {
  1UL << 28, 1UL << 29, 1UL << 31,                   /* LED0..2 on port 1 */
  1UL <<  2, 1UL <<  3, 1UL <<  4, 1UL << 5, 1UL << 6 /* LED3..7 on port 2 */
};

osMutexDef(lcd_mutex);
static osMutexId lcd_mtx;

void Demo_Init (const char *title) {
  LPC_GPIO1->FIODIR |= P1_LEDS;
  LPC_GPIO2->FIODIR |= P2_LEDS;
  LPC_GPIO1->FIOCLR  = P1_LEDS;
  LPC_GPIO2->FIOCLR  = P2_LEDS;

  GLCD_Init();
  GLCD_Clear(Black);
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(Yellow);
  GLCD_DisplayString(0, 0, 1, (unsigned char *)title);

  lcd_mtx = osMutexCreate(osMutex(lcd_mutex));
}

void LED_Set (unsigned int mask) {
  uint32_t p1 = 0, p2 = 0;
  int i;
  for (i = 0; i < 8; i++) {
    if (mask & (1u << i)) {
      if (i < 3) p1 |= led_bit[i];
      else       p2 |= led_bit[i];
    }
  }
  LPC_GPIO1->FIOCLR = P1_LEDS & ~p1;
  LPC_GPIO1->FIOSET = p1;
  LPC_GPIO2->FIOCLR = P2_LEDS & ~p2;
  LPC_GPIO2->FIOSET = p2;
}

void LED_Show (int idx) {
  LED_Set((idx >= 0 && idx < 8) ? (1u << idx) : 0u);
}

void LCD_Text (unsigned int ln, unsigned short color, const char *s) {
  char line[21];
  int i;
  for (i = 0; i < 20 && s[i]; i++) line[i] = s[i];  /* pad to a full line so */
  for (; i < 20; i++)               line[i] = ' ';   /* old text is erased    */
  line[20] = 0;

  osMutexWait(lcd_mtx, osWaitForever);
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(color);
  GLCD_DisplayString(ln, 0, 1, (unsigned char *)line);
  osMutexRelease(lcd_mtx);
}

void LCD_Bar (unsigned int ln, unsigned short color, unsigned int pct) {
  if (pct > 100) pct = 100;
  osMutexWait(lcd_mtx, osWaitForever);
  GLCD_SetBackColor(DarkGrey);
  GLCD_SetTextColor(color);
  GLCD_Bargraph(8, ln * 24 + 4, 304, 16, (pct * 1024u) / 100u);
  GLCD_SetBackColor(Black);
  osMutexRelease(lcd_mtx);
}

void Demo_Spin (unsigned int ms) {
  volatile uint32_t n = ms * 10000u;                 /* ~1 ms per 10k loops @ 100 MHz */
  while (n--) { }
}

#endif /* DEMO */
