#include <stdio.h>
#include "LPC17xx.h"                    // Device header
#include "Board_LED.h"                  // ::Board Support:LED
#include "Board_ADC.h"                  // ::Board Support:A/D Converter
#include "KBD.h"                        // Joystick driver (Lab 1)

// #define _USE_LCD                     // uncomment once GLCD component is added
#ifdef _USE_LCD
  #include "GLCD.h"                     // confirm exact header name once RTE component is added
#endif

char text[10];

extern volatile unsigned char clock_1s;

volatile unsigned short AD_dbg;

uint16_t AD_last;

int main (void) {
  int32_t  res;
  uint32_t AD_avg   = 0;
  uint16_t AD_value = 0;
  uint16_t AD_print = 0;
  uint32_t joy;

  LED_Initialize();
  ADC_Initialize();
  KBD_Init();

#ifdef _USE_LCD
  GLCD_Initialize();
  GLCD_SetForegroundColor(GLCD_COLOR_WHITE);
  GLCD_SetBackgroundColor(GLCD_COLOR_BLUE);
  GLCD_ClearScreen();
  GLCD_DrawString(10, 10, "COE718 Lab 1 - Joystick Demo");
#endif

  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock/100);

  while (1) {

    res = ADC_GetValue();
    if (res != -1) {
      AD_last = res;

      AD_avg += AD_last << 8;
      AD_avg ++;
      if ((AD_avg & 0xFF) == 0x10) {
        AD_value = (AD_avg >> 8) >> 4;
        AD_avg = 0;
      }
    }

    if (AD_value != AD_print) {
      AD_print = AD_value;
      AD_dbg   = AD_value;

      sprintf(text, "0x%04X", AD_value);
    }

    joy = get_button();

    if (joy & KBD_UP) {
      LED_On(0);  LED_Off(1); LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
      GLCD_DrawString(10, 40, "Direction: UP    ");
#endif
    } else if (joy & KBD_DOWN) {
      LED_Off(0); LED_On(1);  LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
      GLCD_DrawString(10, 40, "Direction: DOWN  ");
#endif
    } else if (joy & KBD_LEFT) {
      LED_Off(0); LED_Off(1); LED_On(2);  LED_Off(3);
#ifdef _USE_LCD
      GLCD_DrawString(10, 40, "Direction: LEFT  ");
#endif
    } else if (joy & KBD_RIGHT) {
      LED_Off(0); LED_Off(1); LED_Off(2); LED_On(3);
#ifdef _USE_LCD
      GLCD_DrawString(10, 40, "Direction: RIGHT ");
#endif
    } else if (joy & KBD_SELECT) {
#ifdef _USE_LCD
      GLCD_DrawString(10, 40, "Direction: SELECT");
#endif
    }

    if (clock_1s) {
      clock_1s = 0;

      printf("AD value: %s\r\n", text);
    }
  }
}
