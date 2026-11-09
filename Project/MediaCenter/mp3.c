/*----------------------------------------------------------------------------
 * mp3.c - MP3 player mode: the board becomes a USB speaker for the PC
 *
 *   Adapted from the Keil USBAudio example (usbdmain.c). Changes:
 *   - main() split into mp3_enter() / mp3_exit() so the USB device connects
 *     when the mode opens and disconnects cleanly when it closes, every time.
 *   - Potentiometer direction inverted: counter-clockwise = louder (manual).
 *   - SELECT toggles a local mute, LEDs show a VU (level) meter, and the
 *     screen shows connection status plus volume and level bars.
 *
 *   Audio path: PC --USB isochronous (DMA)--> DataBuf --TIMER0 @ 32 kHz-->
 *               volume scaling --> DAC (P0.26) --> on-board speaker
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include "LPC17xx.h"
#include "type.h"

#include "usb.h"
#include "usbcfg.h"
#include "usbhw.h"
#include "usbcore.h"
#include "usbaudio.h"

#include "app.h"
#include "input.h"
#include "GLCD.h"
#include "LED.h"
#include <stdio.h>

/* 1 = counter-clockwise raises the volume (as the project manual asks).
   If it turns out backwards on the board, set this to 0. */
#define POT_INVERT  1

extern uint32_t SystemFrequency;

/* ---- Globals used by the USB audio code (usbuser.c, adcuser.c) ----------- */
uint8_t  Mute;                                 /* Mute state set by the PC   */
uint32_t Volume;                               /* Volume level               */

#if USB_DMA
uint32_t *InfoBuf = (uint32_t *)(DMA_BUF_ADR);
short    *DataBuf = (short *)(DMA_BUF_ADR + 4*P_C);
#else
uint32_t InfoBuf[P_C];
short    DataBuf[B_S];                         /* Data buffer                */
#endif

uint16_t DataOut;                              /* Data out index             */
uint16_t DataIn;                               /* Data in index              */
uint8_t  DataRun;                              /* Data stream running        */
uint16_t PotVal;                               /* Potentiometer, 0..256      */
uint32_t VUM;                                  /* VU meter accumulator       */
uint32_t Tick;                                 /* Time tick                  */

static uint8_t usb_ready = 0;                  /* USB_Init() already done    */
volatile uint32_t SofCount;                    /* USB frames seen (debug)    */
static volatile uint8_t LocalMute;             /* mute toggled with SELECT   */
static volatile uint8_t VULevel;               /* 0..7 audio level           */

/*
 * Read the potentiometer (AD0.2 on P0.25)
 */
static void get_potval(void) {
  uint32_t val, pot;

  LPC_ADC->CR |= 0x01000000;                   /* Start A/D conversion       */
  do {
    val = LPC_ADC->GDR;                        /* Read A/D data register     */
  } while ((val & 0x80000000) == 0);           /* Wait for end of conversion */
  LPC_ADC->CR &= ~0x01000000;                  /* Stop A/D conversion        */
  pot = ((val >> 8) & 0xF8) + ((val >> 7) & 0x08);   /* 0..256               */
#if POT_INVERT
  pot = 256 - pot;
#endif
  PotVal = (uint16_t)pot;
}

/*
 * Timer 0 interrupt: runs every 31.25 us (32 kHz), outputs one audio sample
 */
void TIMER0_IRQHandler(void) {
  long     val;
  uint32_t cnt;

  if (DataRun) {                               /* Data stream is running     */
    val = DataBuf[DataOut];                    /* Get audio sample           */
    cnt = (DataIn - DataOut) & (B_S - 1);      /* Buffer data count          */
    if (cnt == (B_S - P_C*P_S)) {              /* Too much data in buffer    */
      DataOut++;                               /* Skip one sample            */
    }
    if (cnt > (P_C*P_S)) {                     /* Still enough data          */
      DataOut++;                               /* Update data out index      */
    }
    DataOut &= B_S - 1;                        /* Adjust buffer out index    */
    if (val < 0) VUM -= val;                   /* Accumulate negative value  */
    else         VUM += val;                   /* Accumulate positive value  */
    val  *= Volume;                            /* Apply volume level         */
    val >>= 16;                                /* Adjust value               */
    val  += 0x8000;                            /* Add bias                   */
    val  &= 0xFFFF;                            /* Mask value                 */
  } else {
    val = 0x8000;                              /* DAC middle point (silence) */
  }

  if (Mute || LocalMute) {
    val = 0x8000;
  }

  LPC_DAC->CR = val & 0xFFC0;                  /* Set speaker output         */

  if ((Tick++ & 0x03FF) == 0) {                /* Every 1024th tick (~32 ms) */
    get_potval();
    if (VolCur == 0x8000) {                    /* Minimum level from the PC  */
      Volume = 0;
    } else {
      Volume = VolCur * PotVal;                /* Chained volume level       */
    }
    val = VUM >> 20;                           /* Scale accumulated value    */
    VUM = 0;
    if (val > 7) val = 7;
    VULevel = (uint8_t)val;
  }

  LPC_TIM0->IR = 1;                            /* Clear interrupt flag       */
}

/* ---- Screen --------------------------------------------------------------- */

#define BAR_X   40
#define BAR_W   240
#define BAR_H   16
#define VOL_Y   124
#define VU_Y    176

static void draw_bar(unsigned int y, unsigned int fill, unsigned short col) {
  if (fill > BAR_W) fill = BAR_W;
  GLCD_FillRect(BAR_X, y, fill, BAR_H, col);
  GLCD_FillRect(BAR_X + fill, y, BAR_W - fill, BAR_H, C_ITEM);
}

static void draw_status(uint8_t st) {
  static const char *const msg[3] = {
    " Connect USB to PC  ",
    " Ready: play audio  ",
    "   Now playing...   "
  };
  ui_text_center(56, 1, msg[st], (st == 2) ? C_ACCENT : C_TEXT, C_BG);
}

static void draw_mute(void) {
  GLCD_SetBackColor(C_BG);
  GLCD_SetTextColor(LocalMute ? RGB565(255, 80, 80) : C_BG);
  GLCD_DisplayStringXY(BAR_X + BAR_W - 6 * 5, VOL_Y - 14, 0, (unsigned char *)"MUTED");
}

/* ---- Mode enter / exit ---------------------------------------------------- */

static void mp3_enter(void) {
  uint32_t pclkdiv, pclk;

  /* Screen */
  ui_header("MP3 PLAYER");
  GLCD_SetBackColor(C_BG);
  GLCD_SetTextColor(C_DIM);
  GLCD_DisplayStringXY(BAR_X, VOL_Y - 14, 0, (unsigned char *)"VOLUME (turn the knob)");
  GLCD_DisplayStringXY(BAR_X, VU_Y  - 14, 0, (unsigned char *)"LEVEL");
  ui_footer("knob: volume   SELECT: mute   hold SELECT: back");

  /* Audio state */
  DataRun = 0; DataIn = 0; DataOut = 0; VUM = 0; Tick = 0;
  LocalMute = 0; VULevel = 0; Mute = 0;
  draw_status(0);
  draw_mute();

  /* P0.25 = AD0.2 (potentiometer), P0.26 = AOUT (speaker) */
  LPC_PINCON->PINSEL1 &= ~((0x03 << 18) | (0x03 << 20));
  LPC_PINCON->PINSEL1 |=  ((0x01 << 18) | (0x02 << 20));

  LPC_SC->PCONP |= (1 << 12);                  /* Power the ADC              */
  LPC_ADC->CR = 0x00200E04;                    /* ADC: AIN2, on, ~4 MHz      */
  LPC_DAC->CR = 0x00008000;                    /* DAC to middle point        */
  get_potval();
  Volume = VolCur * PotVal;

  /* Timer 0 at the audio sample rate */
  pclkdiv = (LPC_SC->PCLKSEL0 >> 2) & 0x03;
  switch (pclkdiv) {
    case 0x01: pclk = SystemFrequency;     break;
    case 0x02: pclk = SystemFrequency / 2; break;
    case 0x03: pclk = SystemFrequency / 8; break;
    default:   pclk = SystemFrequency / 4; break;
  }
  LPC_SC->PCONP |= (1 << 1);                   /* Power Timer 0              */
  LPC_TIM0->TCR = 2;                           /* Reset the counter          */
  LPC_TIM0->MR0 = pclk / DATA_FREQ - 1;
  LPC_TIM0->MCR = 3;                           /* Interrupt + reset on MR0   */
  LPC_TIM0->IR  = 1;
  LPC_TIM0->TCR = 1;                           /* Start                      */
  NVIC_SetPriority(TIMER0_IRQn, 1);            /* above SysTick, below USB   */
  NVIC_EnableIRQ(TIMER0_IRQn);

  /* USB: initialise the controller once, then just reconnect on later
     visits (re-initialising it while the PC still knows the device can
     leave enumeration stuck). The PC sees the speaker appear. */
  NVIC_SetPriority(USB_IRQn, 0);
  if (!usb_ready) {
    USB_Init();
    usb_ready = 1;
  } else {
    NVIC_EnableIRQ(USB_IRQn);
  }
  USB_Connect(TRUE);
}

static void mp3_exit(void) {
  USB_Connect(FALSE);                          /* PC sees the speaker leave  */
  NVIC_DisableIRQ(USB_IRQn);
  NVIC_DisableIRQ(TIMER0_IRQn);
  LPC_TIM0->TCR = 0;                           /* Stop Timer 0               */
  LPC_TIM0->IR  = 1;
  DataRun = 0;
  USB_Configuration = 0;                       /* forget the old config      */
  LPC_DAC->CR = 0x00008000;                    /* Silence                    */
  LED_Out(0);
}

void mp3_main(void) {
  uint8_t  ev, st, last_st = 0xFF, last_vu = 0xFF;
  uint16_t last_pot = 0xFFFF;
  uint32_t last_draw = 0;

  mp3_enter();

  for (;;) {
    ev = EV_CODE(input_get());
    if (ev == EV_BACK) break;
    if (ev == EV_SELECT) {
      LocalMute = !LocalMute;
      draw_mute();
    }

    if ((g_ms - last_draw) >= 50) {            /* refresh the screen at 20 Hz */
      last_draw = g_ms;

      st = (USB_Configuration == 0) ? 0 : (DataRun ? 2 : 1);
      if (st != last_st) { draw_status(st); last_st = st; }

      {                                        /* debug line: USB state     */
        char dbg[48];
        sprintf(dbg, "USB cfg:%u  frames:%lu  stream:%u   ",
                USB_Configuration, (unsigned long)SofCount, DataRun);
        GLCD_SetBackColor(C_BG);
        GLCD_SetTextColor(C_DIM);
        GLCD_DisplayStringXY(BAR_X, 204, 0, (unsigned char *)dbg);
      }

      if (PotVal != last_pot) {
        draw_bar(VOL_Y, (uint32_t)PotVal * BAR_W / 256, C_ACCENT);
        last_pot = PotVal;
      }

      {
        uint8_t lvl = (st == 2 && !LocalMute) ? VULevel : 0;
        if (lvl != last_vu) {
          draw_bar(VU_Y, (uint32_t)lvl * BAR_W / 7, RGB565(120, 230, 120));
          LED_Out((1u << lvl) - 1);            /* LEDs as a level meter      */
          last_vu = lvl;
        }
      }
    }
  }

  mp3_exit();
}
