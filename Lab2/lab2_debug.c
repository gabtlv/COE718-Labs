/*----------------------------------------------------------------------------
 * COE718 Lab 2 - DEBUG (Performance Analysis) version
 * Gabrell Talavera
 *
 * Run in the SIMULATOR. No LCD and no delays, so the Performance Analyzer
 * (Show: Functions) measures only the LED code. Each LED method is its own
 * function so it gets its own row in the analyzer.
 *
 * LEDs (MCB1700), two different ports:
 *   LED1 = P1.28   LED2 = P1.29   LED3 = P2.2
 *
 * Conditional execution method: ITE (If-Then-Else) block.
 *   Each method chooses ON/OFF with  "on ? X : Y".  At -O3 armcc compiles this
 *   to  CMP / ITE NE / <op>NE / <op>EQ  (no branch, no pipeline flush).
 *   At -O0 the same C falls back to CMP + conditional branch (BEQ/BNE).
 *
 * Barrel shifter: BarrelAlias() computes the bit-band alias address with
 *   (offset << 5) + (bit << 2), which armcc folds into ADD ..., LSL #n.
 *---------------------------------------------------------------------------*/
#include "LPC17xx.h"
#include <stdio.h>

/*------- ITM Stimulus Port definitions for printf (from bitband.c) --------*/
#define ITM_Port8(n)    (*((volatile unsigned char *)(0xE0000000+4*n)))
#define ITM_Port16(n)   (*((volatile unsigned short*)(0xE0000000+4*n)))
#define ITM_Port32(n)   (*((volatile unsigned long *)(0xE0000000+4*n)))
#define DEMCR           (*((volatile unsigned long *)(0xE000EDFC)))
#define TRCENA          0x01000000

struct __FILE { int handle; };
FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f) {
  if (DEMCR & TRCENA) {
    while (ITM_Port32(0) == 0);
    ITM_Port8(0) = ch;
  }
  return ch;
}
/*--------------------------------------------------------------------------*/

#define LED1_BIT   28u                        /* P1.28 */
#define LED2_BIT   29u                        /* P1.29 */
#define LED3_BIT    2u                        /* P2.2  */
#define P1_LEDS    ((1u << LED1_BIT) | (1u << LED2_BIT))
#define P2_LEDS    (1u << LED3_BIT)

/*--------------------------------------------------------------------------
 * Effective (alias) address calculation  -  UM10360 Table 101 (GPIO map)
 *   LPC_GPIO1 base = 0x2009C020,  LPC_GPIO2 base = 0x2009C040
 *   FIOPIN offset  = 0x14  ->  FIO1PIN = 0x2009C034,  FIO2PIN = 0x2009C054
 *   These are in the SRAM bit-band region (0x20000000 - 0x200FFFFF), so:
 *     alias = 0x22000000 + (byte_offset * 32) + (bit * 4)
 *
 *   LED1 P1.28: offset 0x0009C034 * 0x20 = 0x01380680, 28*4 = 0x70
 *               0x22000000 + 0x01380680 + 0x70 = 0x233806F0
 *   LED2 P1.29: 0x22000000 + 0x01380680 + 0x74 = 0x233806F4
 *   LED3 P2.2 : offset 0x0009C054 * 0x20 = 0x01380A80, 2*4 = 0x08
 *               0x22000000 + 0x01380A80 + 0x08 = 0x23380A88
 *--------------------------------------------------------------------------*/
#define LED1_BB  (*((volatile uint32_t *)0x233806F0))
#define LED2_BB  (*((volatile uint32_t *)0x233806F4))
#define LED3_BB  (*((volatile uint32_t *)0x23380A88))

#define N_TOGGLES  10                         /* on+off cycles per method */

/*------------------------------------------------------------------------*/
void LED_Init(void) {
  LPC_SC->PCONP     |= (1u << 15);            /* power up GPIO            */
  LPC_GPIO1->FIODIR |= P1_LEDS;               /* LED pins = outputs       */
  LPC_GPIO2->FIODIR |= P2_LEDS;
  LPC_GPIO1->FIOCLR  = P1_LEDS;               /* start with LEDs off      */
  LPC_GPIO2->FIOCLR  = P2_LEDS;
}

/* Method 1: MASKING - read, OR/AND with a mask, write back (per port).
 * Conditional: ITE -> ORRNE (set) / BICEQ (clear)                          */
__attribute__((noinline)) void LEDs_Mask(uint32_t on) {
  uint32_t v;

  v = LPC_GPIO1->FIOPIN;
  v = on ? (v | P1_LEDS) : (v & ~P1_LEDS);    /* ITE NE */
  LPC_GPIO1->FIOPIN = v;

  v = LPC_GPIO2->FIOPIN;
  v = on ? (v | P2_LEDS) : (v & ~P2_LEDS);    /* ITE NE */
  LPC_GPIO2->FIOPIN = v;
}

/* Method 2: BitBand() FUNCTION - alias address computed at RUN TIME.     */
__attribute__((noinline)) volatile uint32_t *BitBand(volatile uint32_t *reg, uint32_t bit) {
  uint32_t a = (uint32_t)reg;
  return (volatile uint32_t *)((a & 0xF0000000u) |      /* region (0x2 / 0x4) */
                               0x02000000u |            /* -> alias base      */
                               ((a & 0x000FFFFFu) << 5) |/* byte offset * 32   */
                               (bit << 2));             /* bit number * 4     */
}

__attribute__((noinline)) void LEDs_Function(uint32_t on) {
  uint32_t v = on ? 1u : 0u;                  /* IT(E) NE */
  *BitBand(&LPC_GPIO1->FIOPIN, LED1_BIT) = v;
  *BitBand(&LPC_GPIO1->FIOPIN, LED2_BIT) = v;
  *BitBand(&LPC_GPIO2->FIOPIN, LED3_BIT) = v;
}

/* Method 3: DIRECT BIT BANDING - alias address precomputed (#define),
 * so each LED is a single STR.                                           */
__attribute__((noinline)) void LEDs_Direct(uint32_t on) {
  uint32_t v = on ? 1u : 0u;                  /* IT(E) NE */
  LED1_BB = v;
  LED2_BB = v;
  LED3_BB = v;
}

/* Barrel shifter: alias = base + (offset << 5) + (bit << 2)
 * -> ADD rX, rY, rZ, LSL #5  and  ADD rX, rX, rW, LSL #2               */
__attribute__((noinline)) uint32_t BarrelAlias(uint32_t reg, uint32_t bit) {
  return 0x22000000u + ((reg - 0x20000000u) << 5) + (bit << 2);
}

/*------------------------------------------------------------------------*/
int main(void) {
  int i;
  uint32_t a1, a3;

  LED_Init();

  printf("\nCOE718 Lab 2 - Debug version\n");

  printf("Method 1: Mask\n");
  for (i = 0; i < N_TOGGLES; i++) { LEDs_Mask(1);     LEDs_Mask(0);     }

  printf("Method 2: BitBand() function\n");
  for (i = 0; i < N_TOGGLES; i++) { LEDs_Function(1); LEDs_Function(0); }

  printf("Method 3: Direct bit banding\n");
  for (i = 0; i < N_TOGGLES; i++) { LEDs_Direct(1);   LEDs_Direct(0);   }

  /* Barrel shifter check: recompute two alias addresses and compare */
  a1 = BarrelAlias(0x2009C034u, LED1_BIT);
  a3 = BarrelAlias(0x2009C054u, LED3_BIT);
  printf("\nBarrel shifter: A = 0x22000000 + (off<<5) + (bit<<2)\n");
  printf("P1.28 -> 0x%08X (%s)\n", a1, (a1 == 0x233806F0u) ? "match" : "MISMATCH");
  printf("P2.2  -> 0x%08X (%s)\n", a3, (a3 == 0x23380A88u) ? "match" : "MISMATCH");

  printf("\nDone. Check Performance Analyzer (Show: Functions).\n");
  while (1);
}
