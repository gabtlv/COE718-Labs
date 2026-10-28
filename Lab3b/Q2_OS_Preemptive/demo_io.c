/*----------------------------------------------------------------------------
 * demo_io.c  -  LED / LCD helpers for the DEMO build (COE718 Lab 3b)
 *
 * Airport control tower theme: the runway is the CPU, planes are threads.
 * Compiled only when DEMO = 1 (build_cfg.h). The LCD is shared by several
 * threads, so every LCD access is protected by a mutex: without it a thread
 * switch in the middle of an SPI transfer garbles the screen.
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

/*--------------------------- airport geometry -----------------------------*/
#define SKY_BG     Black
#define GRASS      DarkGreen
#define TARMAC     DarkGrey
#define HOLD_Y     170            /* holding lane: planes 170..189            */
#define RW_Y       192            /* runway 192..231                          */
#define RW_H       40
#define PLANE_W    32
#define PLANE_H    20
#define PLANE_RW_Y (RW_Y + 10)    /* plane on runway: 202..221                */
#define DASH_Y     (RW_Y + 19)    /* centre line 211..212                     */
#define X_START    284            /* touch-down point (right)                 */
#define X_END      8              /* end of the landing roll (left)           */

/* The flight board is drawn from main() before osKernelStart(), when no
   thread is running yet, so the mutex is only used once the kernel runs. */
static void lcd_lock (void)   { if (osKernelRunning()) osMutexWait(lcd_mtx, osWaitForever); }
static void lcd_unlock (void) { if (osKernelRunning()) osMutexRelease(lcd_mtx); }

static int lane_x[2]  = { -1, -1 };       /* where each lane's plane is drawn */
static int lane_id[2] = { -1, -1 };       /* which flight it is               */

static void fill (int x, int y, int w, int h, unsigned short c) {
  if (w <= 0 || h <= 0) return;
  GLCD_SetTextColor(c);
  GLCD_Bargraph((unsigned int)x, (unsigned int)y, (unsigned int)w, (unsigned int)h, 1024);
}

/* repaint the background of a lane inside rectangle (x, y, w, h) */
static void repaint (int lane, int x, int y, int w, int h) {
  int k, dx, x0, x1;
  if (x < 0) { w += x; x = 0; }
  if (x + w > 320) w = 320 - x;
  if (w <= 0) return;
  if (lane == LANE_HOLD) { fill(x, y, w, h, SKY_BG); return; }
  fill(x, y, w, h, TARMAC);
  if (y <= DASH_Y + 1 && y + h > DASH_Y) {        /* centre-line dashes   */
    for (k = 0; k < 12; k++) {
      dx = 16 + k * 24;
      x0 = (dx > x) ? dx : x;
      x1 = (dx + 12 < x + w) ? dx + 12 : x + w;
      if (x1 > x0) fill(x0, DASH_Y, x1 - x0, 2, White);
    }
  }
}

static void draw_runway (void) {
  int k;
  fill(0, HOLD_Y - 2, 320, 24, SKY_BG);
  fill(0, RW_Y - 2, 320, 2, GRASS);
  repaint(LANE_RUNWAY, 0, RW_Y, 320, RW_H);
  fill(0, RW_Y + 2, 320, 1, White);                    /* edge lines        */
  fill(0, RW_Y + RW_H - 3, 320, 1, White);
  for (k = 0; k < 4; k++) {                            /* threshold stripes */
    fill(300, RW_Y + 5 + k * 2, 16, 1, White);
    fill(300, RW_Y + 26 + k * 2, 16, 1, White);
  }
  fill(0, RW_Y + RW_H, 320, 240 - RW_Y - RW_H, GRASS);
}

/* top view of a plane flying left: fuselage, wings, tail, windshield */
static void draw_plane (int x, int y, unsigned short c) {
  fill(x,      y + 8, PLANE_W, 4, c);      /* fuselage   */
  fill(x + 11, y,     6, PLANE_H, c);      /* wings      */
  fill(x + 26, y + 4, 4, 12, c);           /* tail       */
  fill(x + 1,  y + 9, 3, 2, Black);        /* windshield */
}

/*------------------------------ basic I/O ---------------------------------*/
void Demo_Init (const char *title) {
  char line[21];
  int i;
  LPC_GPIO1->FIODIR |= P1_LEDS;
  LPC_GPIO2->FIODIR |= P2_LEDS;
  LPC_GPIO1->FIOCLR  = P1_LEDS;
  LPC_GPIO2->FIOCLR  = P2_LEDS;

  GLCD_Init();
  GLCD_Clear(Black);
  for (i = 0; i < 20 && title[i]; i++) line[i] = title[i];
  for (; i < 20; i++)                   line[i] = ' ';
  line[20] = 0;
  GLCD_SetBackColor(Navy);                             /* title bar         */
  GLCD_SetTextColor(Yellow);
  GLCD_DisplayString(0, 0, 1, (unsigned char *)line);
  GLCD_SetBackColor(Black);
  draw_runway();

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

  lcd_lock();
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(color);
  GLCD_DisplayString(ln, 0, 1, (unsigned char *)line);
  lcd_unlock();
}

void LCD_Bar (unsigned int ln, unsigned short color, unsigned int pct) {
  if (pct > 100) pct = 100;
  lcd_lock();
  GLCD_SetBackColor(DarkGrey);
  GLCD_SetTextColor(color);
  GLCD_Bargraph(8, ln * 24 + 4, 304, 16, (pct * 1024u) / 100u);
  GLCD_SetBackColor(Black);
  lcd_unlock();
}

void Demo_Spin (unsigned int ms) {
  volatile uint32_t n = ms * 10000u;                 /* ~1 ms per 10k loops @ 100 MHz */
  while (n--) { }
}

/*------------------------------ the tower ---------------------------------*/
static void put (char *s, int *n, const char *t, int width) {
  int i = 0;
  while (t[i] && i < width) s[(*n)++] = t[i++];
  while (i++ < width)       s[(*n)++] = ' ';
}

/* "A COE718 P2 LANDING" : tag, callsign (6), priority, status (8) */
void Tower_Flight (int row, unsigned short color, char tag, const char *call,
                   int prio, const char *status) {
  char s[21];
  int n = 0;
  s[n++] = tag;  s[n++] = ' ';
  put(s, &n, call, 6);
  s[n++] = ' ';  s[n++] = 'P';  s[n++] = (char)('0' + prio);  s[n++] = ' ';
  put(s, &n, status, 8);
  s[n] = 0;
  LCD_Text((unsigned int)row, color, s);
}

/* "A COE718 = 66306.000" : the result replaces the status once landed */
void Tower_Result (int row, unsigned short color, char tag, const char *call,
                   double v) {
  char s[21], num[12];
  uint32_t ip, fp;
  int n = 0, k = 0, len;
  if (v < 0) v = -v;
  ip = (uint32_t)v;
  fp = (uint32_t)((v - (double)ip) * 1000.0 + 0.5);
  if (fp >= 1000u) { ip++; fp -= 1000u; }
  do { num[k++] = (char)('0' + ip % 10u); ip /= 10u; } while (ip && k < 7);
  s[n++] = tag;  s[n++] = ' ';
  put(s, &n, call, 6);
  s[n++] = ' ';  s[n++] = '=';
  len = k + 4;                                       /* digits + ".ddd"      */
  while (n < 20 - len) s[n++] = ' ';                 /* right-align          */
  while (k) s[n++] = num[--k];
  s[n++] = '.';
  s[n++] = (char)('0' + fp / 100u);
  s[n++] = (char)('0' + (fp / 10u) % 10u);
  s[n++] = (char)('0' + fp % 10u);
  s[n] = 0;
  LCD_Text((unsigned int)row, color, s);
}

void Tower_Radio (unsigned short color, const char *msg) {
  LCD_Text(6, color, msg);
}

/* Draw flight `id` in a lane at pct = 0 (touch-down) .. 100 (end of roll).
   Moving a plane left only repaints the thin strips it uncovers, so the
   animation does not flicker. A different flight is drawn from scratch. */
void Tower_Plane (int lane, int id, unsigned int pct, unsigned short color) {
  int x, y, d, old;
  if (pct > 100) pct = 100;
  x   = X_START - (int)((X_START - X_END) * pct / 100u);
  y   = (lane == LANE_HOLD) ? HOLD_Y : PLANE_RW_Y;
  old = lane_x[lane];

  lcd_lock();
  if (old >= 0 && (lane_id[lane] != id || x > old || old - x > 12)) {
    repaint(lane, old, y, PLANE_W, PLANE_H);           /* full erase        */
    old = -1;
  }
  if (old >= 0 && old > x) {                           /* uncovered strips  */
    d = old - x;
    repaint(lane, x + PLANE_W,      y + 8, d, 4);      /* behind fuselage   */
    repaint(lane, x + 11 + 6,       y,     d, PLANE_H);/* behind wings      */
    repaint(lane, x + 26 + 4,       y + 4, d, 12);     /* behind tail       */
  }
  draw_plane(x, y, color);
  lane_x[lane]  = x;
  lane_id[lane] = id;
  lcd_unlock();
}

void Tower_Clear (int lane) {
  int y = (lane == LANE_HOLD) ? HOLD_Y : PLANE_RW_Y;
  lcd_lock();
  if (lane_x[lane] >= 0) repaint(lane, lane_x[lane], y, PLANE_W, PLANE_H);
  lane_x[lane]  = -1;
  lane_id[lane] = -1;
  lcd_unlock();
}

/* text over the runway area (lines 7..9) for the final screen */
void Tower_Text (unsigned int ln, unsigned short color, const char *s) {
  if (ln == 7) {                                       /* first use: clear  */
    lcd_lock();
    fill(0, 168, 320, 72, Black);
    lane_x[0] = lane_x[1] = -1;
    lcd_unlock();
  }
  LCD_Text(ln, color, s);
}

#endif /* DEMO */
