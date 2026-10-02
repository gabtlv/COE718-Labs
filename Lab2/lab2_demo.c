/*----------------------------------------------------------------------------
 * COE718 Lab 2 - TARGET (Demo) version:  "BIT BAND RACE"
 * Gabrell Talavera
 *
 * Runs on the MCB1700 board. The three LED methods "race": each one is timed
 * ON THE BOARD with the Cortex-M3 cycle counter (DWT->CYCCNT), then ranked on
 * the LCD with a speed bar. The LEDs blink with each method in turn so the
 * TA can see them, and the LCD shows which method is running + a one-liner.
 *
 * LEDs (MCB1700), two different ports:
 *   LED1 = P1.28   LED2 = P1.29   LED3 = P2.2
 *
 * Conditional execution method: ITE (If-Then-Else) block.
 *   Each method chooses ON/OFF with  "on ? X : Y".  At -O3 armcc compiles this
 *   to  CMP / ITE NE / <op>NE / <op>EQ  (no branch, no pipeline flush).
 *
 * Barrel shifter: BarrelAlias() computes the bit-band alias address with
 *   (offset << 5) + (bit << 2) -> ADD ..., LSL #n. Equation + result on LCD.
 *   Speed bars are scaled with "<< 10" (x1024) for GLCD_Bargraph.
 *
 * LCD: needs GLCD.h + GLCD_SPI_LPC1700.c + Font_6x8_h.h + Font_16x24_h.h.
 *      Comment out _USE_LCD to run in the simulator (the LCD isn't modelled).
 *---------------------------------------------------------------------------*/
#define _USE_LCD

#include "LPC17xx.h"
#include <stdio.h>
#include <string.h>
#ifdef _USE_LCD
  #include "GLCD.h"
#endif

#define LED1_BIT   28u                        /* P1.28 */
#define LED2_BIT   29u                        /* P1.29 */
#define LED3_BIT    2u                        /* P2.2  */
#define P1_LEDS    ((1u << LED1_BIT) | (1u << LED2_BIT))
#define P2_LEDS    (1u << LED3_BIT)

/* Alias addresses - see calculation in lab2_debug.c (UM10360 Table 101)
 *   FIO1PIN = 0x2009C034, FIO2PIN = 0x2009C054 (SRAM bit-band region)
 *   alias = 0x22000000 + (byte_offset * 32) + (bit * 4)                   */
#define LED1_BB  (*((volatile uint32_t *)0x233806F0))   /* P1.28 */
#define LED2_BB  (*((volatile uint32_t *)0x233806F4))   /* P1.29 */
#define LED3_BB  (*((volatile uint32_t *)0x23380A88))   /* P2.2  */

/* Cortex-M3 cycle counter (DWT) - counts CPU clock cycles              */
#define DEMCR_REG   (*((volatile uint32_t *)0xE000EDFC))
#define DWT_CTRL    (*((volatile uint32_t *)0xE0001000))
#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))

#define DELAY_COUNT  2000000u                 /* ~ visible blink rate      */
#define BLINKS       4                        /* blinks per method         */
#define RACE_RUNS    8                        /* timed runs per method     */

/*------------------------------------------------------------------------*/
void LED_Init(void) {
  LPC_SC->PCONP     |= (1u << 15);            /* power up GPIO            */
  LPC_GPIO1->FIODIR |= P1_LEDS;
  LPC_GPIO2->FIODIR |= P2_LEDS;
  LPC_GPIO1->FIOCLR  = P1_LEDS;
  LPC_GPIO2->FIOCLR  = P2_LEDS;
}

void CycleCounter_Init(void) {
  DEMCR_REG |= (1u << 24);                    /* TRCENA: enable DWT       */
  DWT_CYCCNT = 0;
  DWT_CTRL  |= 1u;                            /* CYCCNTENA: start counter */
}

/* Busy-wait delay. Measure ONE call with Execution Profiling for the report. */
__attribute__((noinline)) void Delay(uint32_t n) {
  volatile uint32_t i;
  for (i = 0; i < n; i++);
}

/* Method 1: MASKING (read-modify-write), ITE picks ORR or BIC */
__attribute__((noinline)) void LEDs_Mask(uint32_t on) {
  uint32_t v;
  v = LPC_GPIO1->FIOPIN;
  v = on ? (v | P1_LEDS) : (v & ~P1_LEDS);    /* ITE NE */
  LPC_GPIO1->FIOPIN = v;
  v = LPC_GPIO2->FIOPIN;
  v = on ? (v | P2_LEDS) : (v & ~P2_LEDS);    /* ITE NE */
  LPC_GPIO2->FIOPIN = v;
}

/* Method 2: BitBand() FUNCTION - alias computed at run time */
__attribute__((noinline)) volatile uint32_t *BitBand(volatile uint32_t *reg, uint32_t bit) {
  uint32_t a = (uint32_t)reg;
  return (volatile uint32_t *)((a & 0xF0000000u) | 0x02000000u |
                               ((a & 0x000FFFFFu) << 5) | (bit << 2));
}

__attribute__((noinline)) void LEDs_Function(uint32_t on) {
  uint32_t v = on ? 1u : 0u;                  /* IT(E) NE */
  *BitBand(&LPC_GPIO1->FIOPIN, LED1_BIT) = v;
  *BitBand(&LPC_GPIO1->FIOPIN, LED2_BIT) = v;
  *BitBand(&LPC_GPIO2->FIOPIN, LED3_BIT) = v;
}

/* Method 3: DIRECT BIT BANDING - precomputed alias, one STR per LED */
__attribute__((noinline)) void LEDs_Direct(uint32_t on) {
  uint32_t v = on ? 1u : 0u;                  /* IT(E) NE */
  LED1_BB = v;
  LED2_BB = v;
  LED3_BB = v;
}

/* Barrel shifter: ADD rX, rY, rZ, LSL #5 / LSL #2 */
__attribute__((noinline)) uint32_t BarrelAlias(uint32_t reg, uint32_t bit) {
  return 0x22000000u + ((reg - 0x20000000u) << 5) + (bit << 2);
}

/*------------------------------------------------------------------------*/
typedef void (*led_method_t)(uint32_t on);

typedef struct {
  led_method_t fn;
  const char  *name;       /* 8 chars max              */
  const char  *joke;       /* 20 chars max (LCD width) */
  uint32_t     cycles;     /* measured: one ON + OFF   */
} racer_t;

static racer_t racer[3] = {
  { LEDs_Mask,     "Mask",    "Load. OR. Store. Zzz", 0 },
  { LEDs_Function, "BB Func", "Overthinks every bit", 0 },
  { LEDs_Direct,   "Direct",  "1 store, 0 regrets  ", 0 }
};

/* Time one ON+OFF of a method in CPU cycles (average of RACE_RUNS runs),
 * with the cost of reading the counter itself subtracted.               */
static uint32_t Race_Time(led_method_t fn) {
  uint32_t t0, t1, ovh, total = 0;
  int i;

  t0 = DWT_CYCCNT; t1 = DWT_CYCCNT; ovh = t1 - t0;
  for (i = 0; i < RACE_RUNS; i++) {
    t0 = DWT_CYCCNT;
    fn(1);
    fn(0);
    t1 = DWT_CYCCNT;
    total += (t1 - t0) - ovh;
  }
  return total / RACE_RUNS;
}

/*------------------------------------------------------------------------*/
#define RACE_FRAMES   32         /* frames for the winner to reach the finish */
#define FRAME_DELAY   250000u    /* pause between animation frames            */
#define BAR_X         32         /* bar starts at x = 32 px                   */
#define BAR_W         256        /* ... and the finish line is at x = 288 px  */

/* Each racer's LED lights up when it crosses the finish line (bit banding) */
static volatile uint32_t *const lane_led[3] = {
  (volatile uint32_t *)0x233806F0,            /* Mask    -> LED P1.28 */
  (volatile uint32_t *)0x233806F4,            /* BB Func -> LED P1.29 */
  (volatile uint32_t *)0x23380A88             /* Direct  -> LED P2.2  */
};

#ifdef _USE_LCD
static const unsigned short lane_color[3] = { Yellow, Red, Green };
static const char *const place_str[4] = { "   ", "1st", "2nd", "3rd" };

static void LCD_Line(unsigned int line, const char *s, unsigned short color) {
  GLCD_SetTextColor(color);
  GLCD_DisplayString(line, 0, 1, (unsigned char *)s);   /* 20 chars max */
}

static void LCD_Small(unsigned int line, const char *s) {
  GLCD_SetTextColor(White);
  GLCD_DisplayString(line, 0, 0, (unsigned char *)s);   /* 53 chars max */
}

static void Lane_Text(int i, int place) {
  char buf[24];
  sprintf(buf, "%-8s%4u cyc %s", racer[i].name, (unsigned)racer[i].cycles,
          place_str[place]);                          /* exactly 20 chars */
  LCD_Line(1 + 2 * i, buf, White);
}

static void Lane_Bar(int i, uint32_t progress) {      /* progress 0..1024 */
  GLCD_SetTextColor(lane_color[i]);
  GLCD_Bargraph(BAR_X, (2 + 2 * i) * 24 + 4, BAR_W, 16, progress);
}

/* Animated race: every bar moves at a speed proportional to 1/cycles, so the
 * method with the fewest cycles crosses the finish line first. Runs until
 * the last racer finishes. Racers finishing in the same frame share a place. */
static void Race_Animate(void) {
  uint32_t best = 0xFFFFFFFFu, speed[3], prog;
  int done[3] = { 0, 0, 0 }, place[3] = { 0, 0, 0 };
  int finished = 0, winners = 0, first = -1, i, n;
  uint32_t f;
  char buf[24];

  for (i = 0; i < 3; i++)
    if (racer[i].cycles && racer[i].cycles < best) best = racer[i].cycles;
  if (best == 0xFFFFFFFFu) best = 1;

  for (i = 0; i < 3; i++) {
    /* barrel shifter again: (best << 10) / cycles = speed on a x1024 scale */
    speed[i] = racer[i].cycles ? ((best << 10) / racer[i].cycles) : 1024;
    if (speed[i] < 16) speed[i] = 16;           /* keep the race finite */
    *lane_led[i] = 0;                           /* LED off at the start */
    Lane_Text(i, 0);
    Lane_Bar(i, 0);
    GLCD_SetTextColor(White);                   /* finish-line post */
    GLCD_Bargraph(BAR_X + BAR_W + 1, (2 + 2 * i) * 24, 3, 24, 1024);
  }

  /* 3.. 2.. 1.. GO! */
  LCD_Line(8, "                    ", White);
  for (n = 3; n > 0; n--) {
    sprintf(buf, "   Ready... %d       ", n);
    LCD_Line(7, buf, White);
    Delay(DELAY_COUNT);
  }
  LCD_Line(7, "   GO!!!            ", Green);

  for (f = 1; finished < 3; f++) {
    int newly = 0;
    for (i = 0; i < 3; i++) {
      if (done[i]) continue;
      prog = (speed[i] * f) / RACE_FRAMES;
      if (prog >= 1024) { prog = 1024; done[i] = 1; place[i] = finished + 1; newly++; }
      Lane_Bar(i, prog);
    }
    if (newly) {
      for (i = 0; i < 3; i++)
        if (done[i] && place[i] == finished + 1) {
          Lane_Text(i, place[i]);
          *lane_led[i] = 1;                     /* finisher's LED lights up */
          if (place[i] == 1) { winners++; if (first < 0) first = i; }
        }
      finished += newly;
    }
    Delay(FRAME_DELAY);
  }

  /* Announce the result */
  if (winners > 1) {
    char names[24] = "";
    for (i = 0; i < 3; i++)
      if (place[i] == 1) {
        if (names[0]) strcat(names, " & ");
        strcat(names, racer[i].name);
      }
    LCD_Line(7, "  IT'S A TIE!!!     ", Magenta);
    sprintf(buf, "%-20.20s", names);          /* pad to clear the line */
    LCD_Line(8, buf, Cyan);
  } else {
    sprintf(buf, "WINNER: %-12s", racer[first].name);
    LCD_Line(7, buf, Green);
    LCD_Line(8, racer[first].joke, Cyan);
  }
  Delay(3 * DELAY_COUNT);
  for (i = 0; i < 3; i++) *lane_led[i] = 0;
}
#endif

/*------------------------------------------------------------------------*/
int main(void) {
  char buf[56];
  uint32_t addr;
  int m, b;

  LED_Init();
  CycleCounter_Init();

  /* Barrel shifter: compute LED1's alias address */
  addr = BarrelAlias(0x2009C034u, LED1_BIT);

#ifdef _USE_LCD
  GLCD_Init();
  GLCD_Clear(Blue);
  GLCD_SetBackColor(Blue);

  LCD_Line(0, "COE718 BIT BAND RACE", Yellow);

  /* Bottom (small font): barrel shifter equation + result, ITE note */
  LCD_Small(27, "Barrel shift: A=0x22000000+(off<<5)+(bit<<2)");
  sprintf(buf, "P1.28 -> A=0x%08X  %s", (unsigned)addr,
          (addr == 0x233806F0u) ? "(= #define: MATCH)" : "(MISMATCH)");
  LCD_Small(28, buf);
  LCD_Small(29, "On/off choice uses ITE blocks | LEDs P1.28 P1.29 P2.2");
#endif

  while (1) {
    /* Time every method on the real hardware, then race them */
    for (m = 0; m < 3; m++) racer[m].cycles = Race_Time(racer[m].fn);
#ifdef _USE_LCD
    Race_Animate();
#endif

    /* Show each method blinking the LEDs, with its one-liner */
    for (m = 0; m < 3; m++) {
#ifdef _USE_LCD
      sprintf(buf, "> Now: %-13s", racer[m].name);
      LCD_Line(7, buf, Yellow);
      LCD_Line(8, racer[m].joke, Cyan);
#endif
      for (b = 0; b < BLINKS; b++) {
        racer[m].fn(1);  Delay(DELAY_COUNT);
        racer[m].fn(0);  Delay(DELAY_COUNT);
      }
    }
  }
}

