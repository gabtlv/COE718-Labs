/*----------------------------------------------------------------------------
 * main.c - COE 718 Media Center: start-up, time base and mode state machine
 *
 *   Super-loop design (no RTX). The menu returns the mode the user picked,
 *   main() runs that mode until the user holds SELECT to go back, then
 *   shows the menu again. Each mode sets up and releases its own
 *   peripherals, so only one mode ever owns the USB, DAC or timers.
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include "LPC17xx.h"
#include <string.h>
#include "app.h"
#include "input.h"
#include "GLCD.h"
#include "LED.h"

extern void SystemClockUpdate(void);
extern uint32_t SystemFrequency;               /* set by SystemClockUpdate() */

volatile uint32_t g_ms = 0;

/* SysTick: 1 ms tick. Inputs are polled every 10 ms. Runs at the lowest
   interrupt priority so it never delays the 32 kHz audio interrupt. */
void SysTick_Handler(void) {
  static uint8_t div = 0;
  g_ms++;
  if (++div >= 10) {
    div = 0;
    input_poll();
  }
}

void delay_ms(uint32_t ms) {
  uint32_t start = g_ms;
  while ((g_ms - start) < ms) { }
}

/* ---- Shared drawing helpers --------------------------------------------- */

void ui_text_center(unsigned int y, unsigned char font, const char *s,
                    unsigned short fg, unsigned short bg) {
  unsigned int cw = font ? 16 : 6;
  unsigned int w  = strlen(s) * cw;
  unsigned int x  = (w < SCR_W) ? (SCR_W - w) / 2 : 0;
  GLCD_SetTextColor(fg);
  GLCD_SetBackColor(bg);
  GLCD_DisplayStringXY(x, y, font, (unsigned char *)s);
}

void ui_header(const char *title) {
  GLCD_Clear(C_BG);
  GLCD_FillRect(0, 0, SCR_W, 40, C_HEADER);
  GLCD_FillRect(0, 40, SCR_W, 3, C_ACCENT);
  ui_text_center(8, 1, title, C_TEXT, C_HEADER);
}

void ui_footer(const char *hint) {
  GLCD_FillRect(0, SCR_H - 16, SCR_W, 16, C_HEADER);
  ui_text_center(SCR_H - 12, 0, hint, C_DIM, C_HEADER);
}

/* ---- Main ---------------------------------------------------------------- */

int main(void) {
  app_mode_t mode;

  SystemClockUpdate();                     /* SystemFrequency for mp3.c     */
  LED_Init();
  GLCD_Init();
  input_init();
  SysTick_Config(SystemFrequency / 1000);  /* 1 ms tick                     */

  for (;;) {
    mode = menu_main();
    LED_Out(0);
    input_flush();

    switch (mode) {
      case MODE_GALLERY: gallery_main(); break;
      case MODE_MP3:     mp3_main();     break;
      case MODE_MINES:   mines_main();   break;
      case MODE_FLAPPY:  flappy_main();  break;
      default:                           break;
    }

    LED_Out(0);
    input_flush();
  }
}
