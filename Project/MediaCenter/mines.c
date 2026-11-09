/*----------------------------------------------------------------------------
 * mines.c - Minesweeper
 *
 *   Level screen: UP/DOWN pick Easy / Medium / Hard, SELECT to play.
 *   In game:      joystick moves the cursor (wraps at the edges)
 *                 SELECT reveals the cell; on an opened number whose flags
 *                 are all placed it "chords" (opens the remaining neighbours)
 *                 INT0 places or removes a flag
 *                 hold SELECT goes back to the main menu
 *
 *   - Mines are placed only after the first reveal, never on or next to
 *     that cell, so the first move is always safe and opens an area.
 *   - Zero cells open their neighbours with an ITERATIVE flood fill (a queue),
 *     not recursion, so it cannot overflow the small stack.
 *   - Only cells that change are redrawn.
 *   - LEDs show the number of mines left (mines - flags) in binary.
 *   - Best time per level is kept until reset.
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include "LPC17xx.h"
#include "app.h"
#include "input.h"
#include "GLCD.h"
#include "LED.h"
#include "sound.h"

/* ---- Board geometry ------------------------------------------------------ */
#define CELL      24
#define ROWS      9
#define MAX_COLS  13
#define MAX_CELLS (MAX_COLS * ROWS)
#define GRID_Y    24                     /* status bar is rows 0..23         */

/* ---- Cell bits ------------------------------------------------------------ */
#define F_MINE    0x10
#define F_OPEN    0x20
#define F_FLAG    0x40
#define F_COUNT   0x0F                   /* neighbouring mines, 0..8         */

/* ---- Colours --------------------------------------------------------------- */
#define C_HID      RGB565(118, 128, 168)
#define C_HID_LT   RGB565(184, 192, 222)
#define C_HID_DK   RGB565( 58,  64,  96)
#define C_OPEN     RGB565(208, 212, 224)
#define C_OPEN_LN  RGB565(160, 164, 182)
#define C_BOOM     RGB565(232,  48,  48)
#define C_FLAG     RGB565(230,  40,  40)
#define C_MINE     RGB565( 20,  20,  28)

static const unsigned short num_col[9] = {
  0,
  RGB565( 30,  80, 220), RGB565( 20, 140,  40), RGB565(220,  40,  40),
  RGB565( 20,  30, 140), RGB565(140,  20,  20), RGB565(  0, 130, 130),
  RGB565(  0,   0,   0), RGB565(100, 100, 100)
};

/* ---- Levels ---------------------------------------------------------------- */
typedef struct { const char *name; uint8_t cols; uint8_t mines; } level_t;
static const level_t levels[3] = {
  { "Easy",    9, 10 },
  { "Medium", 11, 16 },
  { "Hard",   13, 22 },
};
#define NO_BEST 0xFFFF
static uint16_t best[3] = { NO_BEST, NO_BEST, NO_BEST };  /* seconds */
static uint8_t  level_sel = 0;

/* ---- Game state ------------------------------------------------------------ */
static uint8_t  cell[MAX_CELLS];
static uint8_t  cols, n_mines, n_cells;
static unsigned int x0;                  /* left edge of the grid           */
static int      cur;                     /* cursor cell                     */
static int      opened, flags;
static uint8_t  started, over;
static uint32_t t_start;
static uint16_t secs;

/* ---- Sounds ------------------------------------------------------------- */
static const note_t snd_win[]  = { {523, 90}, {659, 90}, {784, 90}, {1047, 220} };
static const note_t snd_boom[] = { {NOTE_NOISE, 350}, {110, 150} };

/* ---- Small helpers ------------------------------------------------------- */

static uint32_t rng;
static uint32_t rand32(void) {           /* xorshift32                      */
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

static unsigned int cx(int i) { return x0 + (i % cols) * CELL; }
static unsigned int cy(int i) { return GRID_Y + (i / cols) * CELL; }

/* Fill nb[] with the indices of the neighbours of cell i; returns count */
static int neighbours(int i, int *nb) {
  int r = i / cols, c = i % cols, dr, dc, n = 0;
  for (dr = -1; dr <= 1; dr++) {
    for (dc = -1; dc <= 1; dc++) {
      if ((dr || dc) && r + dr >= 0 && r + dr < ROWS && c + dc >= 0 && c + dc < cols) {
        nb[n++] = (r + dr) * cols + (c + dc);
      }
    }
  }
  return n;
}

/* ---- Drawing --------------------------------------------------------------- */

static void draw_mine(unsigned int x, unsigned int y) {
  unsigned int mx = x + 12, my = y + 12;
  GLCD_FillRect(mx - 1, my - 9, 2, 18, C_MINE);       /* spikes          */
  GLCD_FillRect(mx - 9, my - 1, 18, 2, C_MINE);
  GLCD_FillRect(mx - 5, my - 6, 10, 12, C_MINE);      /* round-ish body  */
  GLCD_FillRect(mx - 6, my - 5, 12, 10, C_MINE);
  GLCD_FillRect(mx - 3, my - 3, 3, 3, C_TEXT);        /* shine           */
}

static void draw_flag(unsigned int x, unsigned int y) {
  GLCD_FillRect(x + 12, y + 5, 2, 14, C_MINE);        /* pole            */
  GLCD_FillRect(x + 7, y + 18, 12, 2, C_MINE);        /* base            */
  GLCD_FillRect(x + 6, y + 5, 6, 7, C_FLAG);          /* flag            */
  GLCD_FillRect(x + 4, y + 7, 2, 3, C_FLAG);
}

static void draw_hidden(unsigned int x, unsigned int y) {
  GLCD_FillRect(x, y, CELL, CELL, C_HID);
  GLCD_FillRect(x, y, CELL, 2, C_HID_LT);             /* raised look     */
  GLCD_FillRect(x, y, 2, CELL, C_HID_LT);
  GLCD_FillRect(x, y + CELL - 2, CELL, 2, C_HID_DK);
  GLCD_FillRect(x + CELL - 2, y, 2, CELL, C_HID_DK);
}

static void draw_cursor(int i) {
  unsigned int x = cx(i), y = cy(i);
  GLCD_FillRect(x, y, CELL, 2, C_ACCENT);
  GLCD_FillRect(x, y + CELL - 2, CELL, 2, C_ACCENT);
  GLCD_FillRect(x, y, 2, CELL, C_ACCENT);
  GLCD_FillRect(x + CELL - 2, y, 2, CELL, C_ACCENT);
}

/* boom: this is the mine that was hit */
static void draw_cell(int i, uint8_t boom) {
  unsigned int x = cx(i), y = cy(i);
  uint8_t v = cell[i];
  char s[2];

  if (v & F_OPEN) {
    GLCD_FillRect(x, y, CELL, CELL, C_OPEN_LN);
    GLCD_FillRect(x + 1, y + 1, CELL - 1, CELL - 1, boom ? C_BOOM : C_OPEN);
    if (v & F_MINE) {
      draw_mine(x, y);
    } else if (v & F_COUNT) {
      s[0] = '0' + (v & F_COUNT);
      s[1] = 0;
      GLCD_SetBackColor(C_OPEN);
      GLCD_SetTextColor(num_col[v & F_COUNT]);
      GLCD_DisplayStringXY(x + 4, y, 1, (unsigned char *)s);
    }
  } else {
    draw_hidden(x, y);
    if (v & F_FLAG) draw_flag(x, y);
  }
  if (i == cur && !over) draw_cursor(i);
}

static void draw_face(const char *f) {
  GLCD_SetBackColor(C_HEADER);
  GLCD_SetTextColor(C_ACCENT);
  GLCD_DisplayStringXY(SCR_W / 2 - 16, 0, 1, (unsigned char *)f);
}

static void draw_counter(void) {
  char s[8];
  int left = n_mines - flags;
  if (left < 0) left = 0;
  sprintf(s, "*%02d", left);
  GLCD_SetBackColor(C_HEADER);
  GLCD_SetTextColor(RGB565(255, 96, 96));
  GLCD_DisplayStringXY(8, 0, 1, (unsigned char *)s);
  LED_Out((uint32_t)left & 0xFF);
}

static void draw_timer(void) {
  char s[8];
  sprintf(s, "%03u", secs);
  GLCD_SetBackColor(C_HEADER);
  GLCD_SetTextColor(C_TEXT);
  GLCD_DisplayStringXY(SCR_W - 8 - 48, 0, 1, (unsigned char *)s);
}

static void draw_banner(const char *line1, const char *line2) {
  GLCD_FillRect(36, 92, 248, 60, C_ACCENT);
  GLCD_FillRect(39, 95, 242, 54, C_HEADER);
  ui_text_center(100, 1, line1, C_TEXT, C_HEADER);
  ui_text_center(132, 0, line2, C_DIM, C_HEADER);
}

/* ---- Game logic ------------------------------------------------------------ */

static void place_mines(int safe) {
  int nb[8], n, i, k, placed = 0;
  static uint8_t keep_clear[MAX_CELLS];

  memset(keep_clear, 0, sizeof(keep_clear));
  keep_clear[safe] = 1;
  n = neighbours(safe, nb);
  for (k = 0; k < n; k++) keep_clear[nb[k]] = 1;

  rng = (g_ms * 2654435761u) ^ SysTick->VAL ^ 0x9E3779B9u;
  if (rng == 0) rng = 1;

  while (placed < n_mines) {
    i = rand32() % n_cells;
    if (!keep_clear[i] && !(cell[i] & F_MINE)) {
      cell[i] |= F_MINE;
      placed++;
    }
  }
  for (i = 0; i < n_cells; i++) {        /* count neighbouring mines        */
    if (cell[i] & F_MINE) continue;
    n = neighbours(i, nb);
    for (k = 0; k < n; k++) {
      if (cell[nb[k]] & F_MINE) cell[i]++;
    }
  }
}

/* Open cell i; if it is a zero, keep opening outward (iterative flood fill) */
static void flood_open(int i) {
  static uint8_t q[MAX_CELLS];          /* static: keep it off the stack   */
  int head = 0, tail = 0, j, k, n, nb[8];

  if (cell[i] & (F_OPEN | F_FLAG)) return;
  cell[i] |= F_OPEN;
  opened++;
  draw_cell(i, 0);
  q[tail++] = (uint8_t)i;

  while (head < tail) {
    j = q[head++];
    if ((cell[j] & F_COUNT) != 0) continue;
    n = neighbours(j, nb);
    for (k = 0; k < n; k++) {
      int m = nb[k];
      if (!(cell[m] & (F_OPEN | F_FLAG | F_MINE))) {
        cell[m] |= F_OPEN;
        opened++;
        draw_cell(m, 0);
        q[tail++] = (uint8_t)m;          /* each cell is queued only once   */
      }
    }
  }
}

static void lose(int hit) {
  int i;

  over = 1;
  sound_play(snd_boom, 2);
  for (i = 0; i < n_cells; i++) {
    if ((cell[i] & F_MINE) && !(cell[i] & F_FLAG)) {
      cell[i] |= F_OPEN;
      draw_cell(i, i == hit);
    } else if ((cell[i] & F_FLAG) && !(cell[i] & F_MINE)) {
      draw_cell(i, 0);                   /* wrong flag: cross it out        */
      GLCD_SetBackColor(C_HID);
      GLCD_SetTextColor(C_BOOM);
      GLCD_DisplayStringXY(cx(i) + 4, cy(i), 1, (unsigned char *)"X");
    }
  }
  if (cur != hit) draw_cell(cur, 0);     /* remove the cursor outline        */
  draw_face(":(");
  for (i = 0; i < 3; i++) {              /* flash the LEDs                   */
    LED_Out(0xFF); delay_ms(120);
    LED_Out(0x00); delay_ms(120);
  }
  draw_banner("BOOM!", "SELECT: play again   hold SELECT: menu");
}

static void win(void) {
  int i;
  char line1[24];
  uint8_t lv = level_sel;

  over = 1;
  sound_play(snd_win, 4);
  for (i = 0; i < n_cells; i++) {        /* flag every mine                  */
    if ((cell[i] & F_MINE) && !(cell[i] & F_FLAG)) {
      cell[i] |= F_FLAG;
      flags++;
      draw_cell(i, 0);
    }
  }
  draw_cell(cur, 0);                     /* removes the cursor outline       */
  draw_counter();
  draw_face("B)");

  if (secs < best[lv]) {
    best[lv] = secs;
    sprintf(line1, "NEW BEST! %us", secs);
  } else {
    sprintf(line1, "YOU WIN! %us", secs);
  }
  draw_banner(line1, "SELECT: play again   hold SELECT: menu");
  for (i = 0; i < 8; i++) {              /* LED chase                        */
    LED_Out(1u << i); delay_ms(60);
  }
  LED_Out(0);
}

static void check_win(void) {
  if (!over && opened == n_cells - n_mines) win();
}

/* SELECT on cell i */
static void do_reveal(int i) {
  int nb[8], n, k, f = 0;

  if (cell[i] & F_FLAG) return;

  if (!started) {                        /* first move: now place the mines */
    place_mines(i);
    started = 1;
    t_start = g_ms;
  }

  if (!(cell[i] & F_OPEN)) {
    if (cell[i] & F_MINE) { lose(i); return; }
    sound_tone(1400, 12);
    flood_open(i);
  } else if (cell[i] & F_COUNT) {        /* chord on an opened number       */
    n = neighbours(i, nb);
    for (k = 0; k < n; k++) if (cell[nb[k]] & F_FLAG) f++;
    if (f != (cell[i] & F_COUNT)) return;
    for (k = 0; k < n; k++) {
      int m = nb[k];
      if (!(cell[m] & (F_OPEN | F_FLAG))) {
        if (cell[m] & F_MINE) { lose(m); return; }
        flood_open(m);
      }
    }
    sound_tone(1400, 12);
  }
  draw_cell(cur, 0);                     /* keep the cursor on top           */
  check_win();
}

static void do_flag(int i) {
  if (cell[i] & F_OPEN) return;
  cell[i] ^= F_FLAG;
  flags += (cell[i] & F_FLAG) ? 1 : -1;
  sound_tone((cell[i] & F_FLAG) ? 880 : 440, 25);
  draw_cell(i, 0);
  draw_counter();
}

static void move_cursor(int dr, int dc) {
  int r = cur / cols, c = cur % cols, old = cur;
  r = (r + dr + ROWS) % ROWS;
  c = (c + dc + cols) % cols;
  cur = r * cols + c;
  draw_cell(old, 0);
  draw_cell(cur, 0);
}

/* Play one game. Returns 1 to go back to the level screen, 0 for the menu */
static int play(void) {
  int i;
  uint8_t ev;
  uint16_t s;

  cols    = levels[level_sel].cols;
  n_mines = levels[level_sel].mines;
  n_cells = cols * ROWS;
  x0      = (SCR_W - cols * CELL) / 2;
  memset(cell, 0, sizeof(cell));
  cur = (ROWS / 2) * cols + cols / 2;
  opened = flags = 0;
  started = over = 0;
  secs = 0;

  GLCD_Clear(C_BG);
  GLCD_FillRect(0, 0, SCR_W, GRID_Y, C_HEADER);
  for (i = 0; i < n_cells; i++) draw_cell(i, 0);
  draw_counter();
  draw_timer();
  draw_face(":)");
  input_flush();

  for (;;) {
    ev = EV_CODE(input_get());

    if (ev == EV_BACK) return 0;

    if (over) {
      if (ev == EV_SELECT) return 1;
      continue;
    }

    switch (ev) {
      case EV_UP:     move_cursor(-1,  0); break;
      case EV_DOWN:   move_cursor( 1,  0); break;
      case EV_LEFT:   move_cursor( 0, -1); break;
      case EV_RIGHT:  move_cursor( 0,  1); break;
      case EV_SELECT: do_reveal(cur);      break;
      case EV_INT0:   do_flag(cur);        break;
      default: break;
    }

    if (started && !over) {
      s = (uint16_t)((g_ms - t_start) / 1000);
      if (s > 999) s = 999;
      if (s != secs) { secs = s; draw_timer(); }
    }
  }
}

/* ---- Level screen ------------------------------------------------------- */

#define LV_X  30
#define LV_W  260
#define LV_H  38
#define LV_Y0 62

static void draw_level(uint8_t i, uint8_t on) {
  char s[24];
  unsigned int y = LV_Y0 + i * (LV_H + 10);
  unsigned short bg = on ? C_ACCENT : C_ITEM;

  GLCD_FillRect(LV_X, y, LV_W, LV_H, bg);
  GLCD_SetBackColor(bg);
  GLCD_SetTextColor(on ? C_DARK : C_TEXT);
  GLCD_DisplayStringXY(LV_X + 14, y + 7, 1, (unsigned char *)levels[i].name);

  sprintf(s, "%ux%u, %u mines", levels[i].cols, ROWS, levels[i].mines);
  GLCD_SetTextColor(on ? C_HEADER : C_DIM);
  GLCD_DisplayStringXY(LV_X + 130, y + 8, 0, (unsigned char *)s);
  if (best[i] != NO_BEST) sprintf(s, "best: %us", best[i]);
  else         sprintf(s, "best: --");
  GLCD_DisplayStringXY(LV_X + 130, y + 22, 0, (unsigned char *)s);
}

/* Returns 1 to play, 0 to go back to the menu */
static int choose_level(void) {
  uint8_t i, ev, old;

  ui_header("MINESWEEPER");
  ui_text_center(46, 0, "move: joystick   reveal: SELECT   flag: INT0", C_DIM, C_BG);
  for (i = 0; i < 3; i++) draw_level(i, i == level_sel);
  ui_footer("UP/DOWN: level   SELECT: play   hold SELECT: back");
  LED_Out(0);
  input_flush();

  for (;;) {
    ev = EV_CODE(input_get());
    if (ev == EV_BACK)   return 0;
    if (ev == EV_SELECT) return 1;
    if (ev == EV_UP || ev == EV_DOWN) {
      old = level_sel;
      level_sel = (ev == EV_UP) ? (level_sel + 2) % 3 : (level_sel + 1) % 3;
      draw_level(old, 0);
      draw_level(level_sel, 1);
    }
  }
}

void mines_main(void) {
  sound_init();
  while (choose_level()) {
    if (!play()) break;
  }
  sound_off();
  LED_Out(0);
}
