#include "LPC17xx.h"
#include "LED.h"
#include "Board_ADC.h"
#include "KBD.h"

#define _USE_LCD           // comment this out when running in Debug/simulation mode
#ifdef _USE_LCD
  #include "GLCD.h"
#endif

int main (void) {
  uint32_t joy;
  uint32_t last_joy = 0xFFFFFFFF;   // forces the LCD to draw on the very first loop

  LED_Init();
  ADC_Initialize();        // still required: IRQ.c's SysTick handler still triggers
                            // ADC conversions each tick, even though we ignore the result
  KBD_Init();

#ifdef _USE_LCD
  GLCD_Initialize();
  GLCD_SetForegroundColor(GLCD_COLOR_WHITE);
  GLCD_SetBackgroundColor(GLCD_COLOR_BLUE);
  GLCD_ClearScreen();
  GLCD_DrawString(10, 10, "COE718 Lab 1");
  GLCD_DrawString(10, 25, "Joystick Direction Demo");
#endif

  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock/100);

  while (1) {
    joy = get_button();

    if (joy != last_joy) {         // only touch LEDs/LCD when direction changes
      last_joy = joy;

      if (joy & KBD_UP) {
        LED_On(0);  LED_Off(1); LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
        GLCD_DrawString(10, 50, "Direction: UP    ");
#endif
      } else if (joy & KBD_DOWN) {
        LED_Off(0); LED_On(1);  LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
        GLCD_DrawString(10, 50, "Direction: DOWN  ");
#endif
      } else if (joy & KBD_LEFT) {
        LED_Off(0); LED_Off(1); LED_On(2);  LED_Off(3);
#ifdef _USE_LCD
        GLCD_DrawString(10, 50, "Direction: LEFT  ");
#endif
      } else if (joy & KBD_RIGHT) {
        LED_Off(0); LED_Off(1); LED_Off(2); LED_On(3);
#ifdef _USE_LCD
        GLCD_DrawString(10, 50, "Direction: RIGHT ");
#endif
      } else if (joy & KBD_SELECT) {
        LED_Off(0); LED_Off(1); LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
        GLCD_DrawString(10, 50, "Direction: SELECT");
#endif
      }
    }
  }
}
