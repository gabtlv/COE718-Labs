#include "LPC17xx.h"
#include "LED.h"
#include "Board_ADC.h"
#include "KBD.h"

#define _USE_LCD
#ifdef _USE_LCD
  #include "GLCD.h"
#endif

int main (void) {
  uint32_t joy;
  uint32_t last_joy = 0xFFFFFFFF;

  LED_Init();
  ADC_Initialize();
  KBD_Init();

#ifdef _USE_LCD
  GLCD_Init();
  GLCD_Clear(Blue);
  GLCD_SetBackColor(White);
  GLCD_SetTextColor(Black);
  GLCD_DisplayString(0, 0, 1, (unsigned char *)"     COE718 Lab 1     ");
  GLCD_DisplayString(1, 0, 1, (unsigned char *)"  Joystick Direction  ");

	GLCD_SetBackColor(Blue);
	GLCD_SetTextColor(White);
	GLCD_DisplayString(4, 0, 1, (unsigned char *)"Direction: NONE");
#endif

  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock/100);

  while (1) {
    joy = get_button();

    if (joy != last_joy) {
      last_joy = joy;

      if (joy & KBD_UP) {
        LED_On(0);  LED_Off(1); LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
				GLCD_ClearLn(3, 1);
        GLCD_DisplayString(4, 0, 1, (unsigned char *)"Direction: UP     ");
#endif
      } else if (joy & KBD_DOWN) {
        LED_Off(0); LED_On(1);  LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
        GLCD_DisplayString(4, 0, 1, (unsigned char *)"Direction: DOWN   ");
#endif
      } else if (joy & KBD_LEFT) {
        LED_Off(0); LED_Off(1); LED_On(2);  LED_Off(3);
#ifdef _USE_LCD
        GLCD_DisplayString(4, 0, 1, (unsigned char *)"Direction: LEFT   ");
#endif
      } else if (joy & KBD_RIGHT) {
        LED_Off(0); LED_Off(1); LED_Off(2); LED_On(3);
#ifdef _USE_LCD
        GLCD_DisplayString(4, 0, 1, (unsigned char *)"Direction: RIGHT  ");
#endif
      } else if (joy & KBD_SELECT) {
        LED_Off(0); LED_Off(1); LED_Off(2); LED_Off(3);
#ifdef _USE_LCD
        GLCD_DisplayString(4, 0, 1, (unsigned char *)"Direction: SELECT ");
#endif
      }
    }
  }
}
