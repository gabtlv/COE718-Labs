/*----------------------------------------------------------------------------
 * demo_io.c  -  LED / LCD helpers for the DEMO build (COE718 Lab 4, Part 1)
 *
 * RMS SCOPE: PLAN trace (calculated) above the LIVE trace (measured).
 * Compiled only when DEMO = 1 (build_cfg.h). The LCD is shared by the three
 * tasks, so every LCD access is protected by a mutex: a task switch in the
 * middle of an SPI transfer would otherwise garble the screen.
 *
 * All drawing is done by the running task inside its own job, so its cost
 * is part of the job's CPU time and the timeline is not disturbed.
 *---------------------------------------------------------------------------*/
#include "build_cfg.h"

#if DEMO

#include "LPC17xx.h"
#include "cmsis_os.h"
#include "GLCD.h"
#include "demo_io.h"

/*------------------------------- LEDs -------------------------------------*/
#define P1_LEDS   ((1UL << 28) | (1UL << 29) | (1UL << 31))
#define P2_LEDS   ((1UL <<  2) | (1UL <<  3) | (1UL <<  4) | (1UL << 5) | (1UL << 6))

static const uint32_t led_bit[8] = {
  1UL << 28, 1UL << 29, 1UL << 31,                    /* LED0..2 on port 1 */
  1UL <<  2, 1UL <<  3, 1UL <<  4, 1UL << 5, 1UL << 6 /* LED3..7 on port 2 */
};

static void LED_Set (unsigned int mask) {
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

/*------------------------------ layout ------------------------------------*/
/* 16x24 font: line n is y = 24n.   6x8 font: line n is y = 8n, col c x = 6c */
#define X0        12            /* left edge of both traces                  */
#define XW        300           /* trace width in pixels = one hyperperiod   */
#define PLAN_LN   5             /* 6x8 lines 5,6,7  = PLAN rows C, B, A      */
#define LIVE_LN   10            /* 6x8 lines 10,11,12 = LIVE rows C, B, A    */
#define AXIS_LN   13            /* tick marks; labels on line 14             */

static const unsigned short col_live[3] = { Cyan,     Magenta, Yellow };   /* A B C */
static const unsigned short col_plan[3] = { DarkCyan, Purple,  Olive  };
static const char           tag[3]      = { 'A', 'B', 'C' };
static const int            lab_prio[3] = { 3, 2, 1 };

static uint32_t hyper_ms;       /* LCM of the periods (4000 ms)               */
static int      live_px = XW - 1;   /* last LIVE column drawn                */
static int      led_task = -1, led_lvl = -1;

osMutexDef(lcd_mutex);
static osMutexId lcd_mtx;

/* Demo_Init runs before the kernel starts, so the mutex is only used once
   the kernel is running. */
static void lcd_lock (void)   { if (osKernelRunning()) osMutexWait(lcd_mtx, osWaitForever); }
static void lcd_unlock (void) { if (osKernelRunning()) osMutexRelease(lcd_mtx); }

static void fill (int x, int y, int w, int h, unsigned short c) {
  if (w <= 0 || h <= 0) return;
  GLCD_SetTextColor(c);
  GLCD_Bargraph((unsigned int)x, (unsigned int)y, (unsigned int)w, (unsigned int)h, 1024);
}

/* y of task i's row in a trace: C (priority 1) on top, A at the bottom */
static int row_y (int base_ln, int i) { return (base_ln + (2 - i)) * 8; }

static void small (unsigned int ln, unsigned int col, unsigned short c, const char *s) {
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(c);
  GLCD_DisplayString(ln, col, 0, (unsigned char *)s);
}

/* full 20-character line in the 16x24 font (padded so old text is erased) */
static void big (unsigned int ln, unsigned short color, const char *s) {
  char line[21];
  int i;
  for (i = 0; i < 20 && s[i]; i++) line[i] = s[i];
  for (; i < 20; i++)               line[i] = ' ';
  line[20] = 0;
  lcd_lock();
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(color);
  GLCD_DisplayString(ln, 0, 1, (unsigned char *)line);
  lcd_unlock();
}

/* append unsigned number */
static void put_u (char *s, int *n, uint32_t v) {
  char d[10];
  int k = 0;
  do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 10);
  while (k) s[(*n)++] = d[--k];
}

/* append milliseconds as seconds with one decimal: 1500 -> "1.5" */
static void put_s (char *s, int *n, uint32_t ms) {
  put_u(s, n, ms / 1000u);
  s[(*n)++] = '.';
  s[(*n)++] = (char)('0' + (ms % 1000u) / 100u);
}

static uint32_t gcd (uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }

/*------------------------------ PLAN trace --------------------------------*/
/* Simulate RMS for one hyperperiod in 1 ms steps: at every release the job's
   computation time is added to the task's remaining work, and each ms goes
   to the ready task with the best (lowest) lab priority. Same rule RTX uses. */
static void draw_plan (const uint32_t T[3], const uint32_t C[3]) {
  uint32_t rem[3] = { 0, 0, 0 }, t;
  int i, who, run_who = -2, run_px = 0, px;

  for (t = 0; t <= hyper_ms; t++) {
    who = -1;
    if (t < hyper_ms) {
      for (i = 0; i < 3; i++) if (t % T[i] == 0u) rem[i] += C[i];
      for (i = 0; i < 3; i++)
        if (rem[i] && (who < 0 || lab_prio[i] < lab_prio[who])) who = i;
      if (who >= 0) rem[who]--;
    }
    px = (int)(t * XW / hyper_ms);
    if (who != run_who || t == hyper_ms) {         /* draw the finished run */
      if (run_who >= 0) fill(X0 + run_px, row_y(PLAN_LN, run_who) + 1, px - run_px, 6, col_plan[run_who]);
      run_who = who;
      run_px  = px;
    }
  }
}

/*--------------------------------- API ------------------------------------*/
void Demo_Init (const uint32_t T[3], const uint32_t C[3], uint32_t unit_us) {
  uint32_t Tms[3], Cms[3], k;
  char lab[4];
  int i, x;

  for (i = 0; i < 3; i++) {
    Tms[i] = T[i] * unit_us / 1000u;
    Cms[i] = C[i] * unit_us / 1000u;
  }
  hyper_ms = Tms[0];
  for (i = 1; i < 3; i++) hyper_ms = hyper_ms / gcd(hyper_ms, Tms[i]) * Tms[i];

  LPC_GPIO1->FIODIR |= P1_LEDS;
  LPC_GPIO2->FIODIR |= P2_LEDS;
  LED_Set(0);

  GLCD_Init();
  GLCD_Clear(Black);
  GLCD_SetBackColor(Navy);
  GLCD_SetTextColor(Yellow);
  GLCD_DisplayString(0, 0, 1, (unsigned char *)" RMS SCOPE   U=1.00 ");

  /* legend: task, lab priority, period / computation in seconds */
  for (i = 0; i < 3; i++) {
    char s[16];
    int n = 0;
    s[n++] = tag[i]; s[n++] = ' '; s[n++] = 'P'; s[n++] = (char)('0' + lab_prio[i]);
    s[n++] = ' '; s[n++] = 'T'; put_s(s, &n, Tms[i]);
    s[n++] = ' '; s[n++] = 'C'; put_s(s, &n, Cms[i]);
    s[n] = 0;
    small(3, (unsigned int)(i * 18), col_live[i], s);
  }

  small(PLAN_LN - 1, 2, LightGrey, "PLAN: calculated RMS");
  small(LIVE_LN - 1, 2, LightGrey, "LIVE: RTX on the board");
  for (i = 0; i < 3; i++) {
    lab[0] = tag[i]; lab[1] = 0;
    small((unsigned int)(row_y(PLAN_LN, i) / 8), 0, col_live[i], lab);
    small((unsigned int)(row_y(LIVE_LN, i) / 8), 0, col_live[i], lab);
  }
  draw_plan(Tms, Cms);

  /* time axis: a tick every 500 ms, a label every second, deadlines in red */
  for (k = 0; k <= hyper_ms; k += 500u) {
    x = X0 + (int)(k * XW / hyper_ms);
    if (x > X0 + XW - 1) x = X0 + XW - 1;
    fill(x, AXIS_LN * 8, 1, (k % 1000u) ? 3 : 6, LightGrey);
    for (i = 0; i < 3; i++)
      if (k % Tms[i] == 0u) fill(x, AXIS_LN * 8 + 6, 1, 2, Red);  /* release / deadline */
    if (k % 1000u == 0u) {
      int n = 0;
      char s[4];
      put_u(s, &n, k / 1000u);
      if (k == hyper_ms) s[n++] = 's';
      s[n] = 0;
      small(AXIS_LN + 1, (unsigned int)((x > 3 ? x - 3 : 0) / 6), LightGrey, s);
    }
  }

  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(LightGrey);
  GLCD_DisplayString(9, 0, 1, (unsigned char *)"LED A7 B6 C5 0-4=% ");

  lcd_mtx = osMutexCreate(osMutex(lcd_mutex));
}

/* Task i got the CPU (start of a job, or back after a pre-emption). */
void Demo_Run (int i, uint32_t job, int resumed) {
  char s[21];
  int n = 0;
  s[n++] = 'N'; s[n++] = 'O'; s[n++] = 'W'; s[n++] = ':'; s[n++] = ' ';
  s[n++] = tag[i]; s[n++] = ' '; s[n++] = ' ';
  if (resumed) {
    const char *r = "resumes";
    while (*r) s[n++] = *r++;
  } else {
    s[n++] = 'j'; s[n++] = 'o'; s[n++] = 'b'; s[n++] = ' ';
    put_u(s, &n, job);
  }
  s[n] = 0;
  led_task = -1;                                   /* force an LED update   */
  big(5, col_live[i], s);
}

/* Called on every pass of delay(): keeps the LED bar and the LIVE trace
   up to date. Cheap when nothing changed. */
void Demo_Tick (int i, uint32_t used, uint32_t need, uint32_t t_ms) {
  int px, x, k, r, steps;
  int lvl = 1 + (int)((uint64_t)used * 5u / need);
  if (lvl > 5) lvl = 5;

  if (i != led_task || lvl != led_lvl) {
    LED_Set(((1u << lvl) - 1u) | (1u << (7 - i)));  /* bar + A7 / B6 / C5   */
    led_task = i;
    led_lvl  = lvl;
  }

  px = (int)((t_ms % hyper_ms) * XW / hyper_ms);
  if (px == live_px) return;

  /* draw every column from the last one drawn up to px (wrapping at the
     end of the hyperperiod) in this task's colour, then the cursor */
  steps = px - live_px;
  if (steps < 0) steps += XW;
  if (steps > 20) steps = 1;                       /* first draw / long gap */
  lcd_lock();
  for (k = 0; k < steps; k++) {
    x = (px - steps + 1 + k + XW) % XW;
    for (r = 0; r < 3; r++)
      fill(X0 + x, row_y(LIVE_LN, r) + 1, 1, 6, (r == i) ? col_live[i] : Black);
  }
  x = (px + 1) % XW;
  fill(X0 + x, LIVE_LN * 8, 2, 24, White);          /* sweeping cursor       */
  lcd_unlock();
  live_px = px;
}

/* At the start of each job: jobs done, deadline misses, worst response. */
void Demo_Stats (const volatile uint32_t jobs[3], const volatile uint32_t miss[3],
                 const volatile uint32_t resp_ms[3]) {
  char s[21];
  int n, i;
  uint32_t total = 0;

  n = 0; s[n++] = 'j'; s[n++] = 'o'; s[n++] = 'b'; s[n++] = 's';
  for (i = 0; i < 3; i++) { s[n++] = ' '; s[n++] = tag[i]; put_u(s, &n, jobs[i]); }
  s[n] = 0;
  big(6, White, s);

  n = 0; s[n++] = 'm'; s[n++] = 'i'; s[n++] = 's'; s[n++] = 's';
  for (i = 0; i < 3; i++) { s[n++] = ' '; s[n++] = tag[i]; put_u(s, &n, miss[i]); total += miss[i]; }
  s[n] = 0;
  big(7, total ? Red : Green, s);

  n = 0; s[n++] = 'r'; s[n++] = 'e'; s[n++] = 's'; s[n++] = 'p';
  for (i = 0; i < 3; i++) { s[n++] = ' '; s[n++] = tag[i]; put_s(s, &n, resp_ms[i]); }
  s[n] = 0;
  big(8, LightGrey, s);
}

#endif /* DEMO */
