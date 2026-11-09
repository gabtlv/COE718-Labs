/*----------------------------------------------------------------------------
 * flappy.c - Flappy Bird (bonus game)
 *
 *   UP or INT0: flap        SELECT (after a crash): retry
 *   hold SELECT: back to the main menu
 *
 *   - 30 frames per second, timed from the 1 ms SysTick counter.
 *   - Physics in 1/16-pixel fixed point (no floating point):
 *       each frame  vy += GRAVITY, y += vy;   a flap sets vy = FLAP_V.
 *   - The screen is never fully redrawn during play. Pipes scroll left
 *     2 px a frame by repainting only thin strips at their edges, and the
 *     bird erases only the strip it moved out of. This keeps the slow SPI
 *     LCD flicker-free.
 *   - The bird is 16x12 pixel art with two wing frames, built into RGB565
 *     bitmaps at start-up.
 *   - LEDs show the score in binary; sounds on flap, point and crash.
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

/* ---- Layout ---------------------------------------------------------------- */
#define HUD_H     24
#define PLAY_Y0   HUD_H
#define GROUND_Y  216
#define BIRD_X    64
#define BIRD_W    16
#define BIRD_H    12

/* ---- Tuning ---------------------------------------------------------------- */
#define FRAME_MS  33          /* ~30 fps                                      */
#define GRAVITY   5           /* 1/16 px per frame^2                          */
#define FLAP_V    (-78)       /* 1/16 px per frame                            */
#define MAX_V     112
#define PIPE_W    34          /* must be even (pipes move 2 px a frame)       */
#define SPEED     2
#define GAP       70          /* opening between top and bottom pipe          */
#define SPACING   150         /* distance between pipes                       */
#define N_PIPES   3

/* ---- Colours --------------------------------------------------------------- */
#define C_SKY     RGB565(112, 197, 206)
#define C_PIPE    RGB565( 96, 190,  60)
#define C_PIPE_DK RGB565( 40, 110,  30)
#define C_GRASS   RGB565( 90, 200,  60)
#define C_DIRT    RGB565(222, 200, 140)
#define C_DIRT_DK RGB565(200, 170, 110)

/* ---- Bird sprite (16 x 12, two wing frames) ------------------------------ */
static const char *const bird_art[2][BIRD_H] = {
  { "....KKKKKK......",
    "..KKYYYYWWKK....",
    ".KYYYYYWWWWWK...",
    "KwwwwYYWWWKWK...",
    "KwwwwwYWWWKWK...",
    "KwwwwwYYWWWWK...",
    ".KwwwYYYYKKKKKK.",
    "..KYYYYYKOOOOOOK",
    "..KYYYYKOKKKKKK.",
    "...KKYYYKOOOOOK.",
    ".....KKKKKKKKK..",
    "................" },
  { "....KKKKKK......",
    "..KKYYYYWWKK....",
    ".KYYYYYWWWWWK...",
    "KYYYYYYWWWKWK...",
    "KYYYYYYWWWKWK...",
    "KwwwwwYYWWWWK...",
    "KwwwwwwYKKKKKK..",
    ".KwwwwYKOOOOOOK.",
    "..KKKYYKOKKKKKK.",
    "...KKYYYKOOOOOK.",
    ".....KKKKKKKKK..",
    "................" }
};
static unsigned short bird_px[2][BIRD_W * BIRD_H];   /* rows bottom-up */

static void build_bird(void) {
  int f, r, c;
  unsigned short col;
  for (f = 0; f < 2; f++) {
    for (r = 0; r < BIRD_H; r++) {
      for (c = 0; c < BIRD_W; c++) {
        switch (bird_art[f][r][c]) {
          case 'K': col = RGB565( 30,  30,  30); break;
          case 'Y': col = RGB565(250, 210,  40); break;
          case 'W': col = RGB565(255, 255, 255); break;
          case 'w': col = RGB565(255, 240, 200); break;
          case 'O': col = RGB565(240, 110,  40); break;
          default:  col = C_SKY;                 break;
        }
        /* GLCD_Bitmap draws the last row first, so store rows flipped */
        bird_px[f][(BIRD_H - 1 - r) * BIRD_W + c] = col;
      }
    }
  }
}

/* ---- State ----------------------------------------------------------------- */
typedef struct { int x; int gap; uint8_t scored; } pipe_t;

static pipe_t   pipes[N_PIPES];
static int      yq, vy;                  /* bird y and speed, 1/16 px     */
static int      by;                      /* bird y on screen (top edge)   */
static uint8_t  wing;
static uint16_t score, best_score;
static uint32_t rng = 1;

static const note_t snd_crash[] = { {NOTE_NOISE, 250}, {220, 80}, {165, 160} };

static uint32_t rand32(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

static int new_gap(void) {
  int lo = PLAY_Y0 + 16, hi = GROUND_Y - 16 - GAP;
  return lo + (int)(rand32() % (uint32_t)(hi - lo + 1));
}

/* Fill a rectangle, clipped to the screen width (pipes slide off the edges) */
static void rect_clip(int x, int y, int w, int h, unsigned short c) {
  if (x < 0)          { w += x; x = 0; }
  if (x + w > SCR_W)  { w = SCR_W - x; }
  if (w <= 0 || h <= 0) return;
  GLCD_FillRect((unsigned int)x, (unsigned int)y, (unsigned int)w, (unsigned int)h, c);
}

/* Paint columns [x, x+w) of a pipe (top part and bottom part) */
static void pipe_strip(const pipe_t *p, int x, int w, unsigned short c) {
  rect_clip(x, PLAY_Y0, w, p->gap - PLAY_Y0, c);
  rect_clip(x, p->gap + GAP, w, GROUND_Y - (p->gap + GAP), c);
}

/* Move a pipe 2 px left, repainting only its edges */
static void pipe_step(pipe_t *p) {
  p->x -= SPEED;
  pipe_strip(p, p->x,              2, C_PIPE_DK);   /* new left edge       */
  pipe_strip(p, p->x + 2,          2, C_PIPE);      /* old left edge->body */
  pipe_strip(p, p->x + PIPE_W - 2, 2, C_PIPE_DK);   /* new right edge      */
  pipe_strip(p, p->x + PIPE_W,     2, C_SKY);       /* old right edge->sky */
}

static void draw_bird(int new_y) {
  int old_y = by;
  int top, h;

  if (new_y > old_y) {                   /* moved down: clear the strip above */
    h = new_y - old_y;
    if (h > BIRD_H) h = BIRD_H;
    GLCD_FillRect(BIRD_X, old_y, BIRD_W, h, C_SKY);
  } else if (new_y < old_y) {            /* moved up: clear the strip below   */
    top = new_y + BIRD_H;
    if (top < old_y) top = old_y;
    h = old_y + BIRD_H - top;
    if (h > 0) GLCD_FillRect(BIRD_X, top, BIRD_W, h, C_SKY);
  }
  by = new_y;
  GLCD_Bitmap(BIRD_X, by, BIRD_W, BIRD_H, (unsigned char *)bird_px[wing]);
}

static void draw_hud(void) {
  char s[16];
  GLCD_SetBackColor(C_HEADER);
  GLCD_SetTextColor(C_TEXT);
  sprintf(s, "SCORE %u", score);
  GLCD_DisplayStringXY(8, 0, 1, (unsigned char *)s);
  GLCD_SetTextColor(C_ACCENT);
  sprintf(s, "BEST %u", best_score);
  GLCD_DisplayStringXY(SCR_W - 8 - 16 * strlen(s), 0, 1, (unsigned char *)s);
  LED_Out(score & 0xFF);
}

static void draw_scene(void) {
  int x;
  GLCD_FillRect(0, 0, SCR_W, HUD_H, C_HEADER);
  GLCD_FillRect(0, PLAY_Y0, SCR_W, GROUND_Y - PLAY_Y0, C_SKY);
  GLCD_FillRect(0, GROUND_Y, SCR_W, 4, C_GRASS);
  GLCD_FillRect(0, GROUND_Y + 4, SCR_W, SCR_H - GROUND_Y - 4, C_DIRT);
  for (x = 0; x < SCR_W; x += 24) {      /* a little texture in the dirt  */
    GLCD_FillRect(x + 4, GROUND_Y + 10, 10, 3, C_DIRT_DK);
    GLCD_FillRect(x + 16, GROUND_Y + 18, 6, 3, C_DIRT_DK);
  }
  draw_hud();
}

static uint8_t hit_pipe(void) {
  int i;
  for (i = 0; i < N_PIPES; i++) {
    const pipe_t *p = &pipes[i];
    if (BIRD_X + BIRD_W > p->x && BIRD_X < p->x + PIPE_W) {
      if (by < p->gap || by + BIRD_H > p->gap + GAP) return 1;
    }
  }
  return 0;
}

static void reset_game(void) {
  int i;
  score = 0;
  yq = ((PLAY_Y0 + GROUND_Y) / 2 - BIRD_H / 2) << 4;
  vy = 0;
  by = yq >> 4;
  wing = 0;
  for (i = 0; i < N_PIPES; i++) {
    pipes[i].x = SCR_W + i * SPACING;
    pipes[i].gap = new_gap();
    pipes[i].scored = 0;
  }
  draw_scene();
  GLCD_Bitmap(BIRD_X, by, BIRD_W, BIRD_H, (unsigned char *)bird_px[0]);
}

/* Waiting screen: the bird bobs until the first flap.
   Returns 1 on a flap, 0 if the player went back to the menu. */
static int wait_for_start(void) {
  static const signed char bob[12] = { 0, 1, 2, 3, 3, 2, 1, 0, -1, -2, -2, -1 };
  int k = 0, base = by;
  uint32_t last = g_ms;
  uint8_t ev;

  ui_text_center(150, 1, "UP or INT0", C_DARK, C_SKY);
  ui_text_center(176, 0, "to flap", C_DARK, C_SKY);
  input_flush();

  for (;;) {
    ev = input_get();
    if (EV_CODE(ev) == EV_BACK) return 0;
    if (ev == EV_UP || ev == EV_INT0) break;
    if (g_ms - last >= 80) {
      last = g_ms;
      k = (k + 1) % 12;
      wing = (k / 3) & 1;
      draw_bird(base + bob[k]);
    }
  }
  GLCD_FillRect(0, 150, SCR_W, 24, C_SKY);   /* remove the prompt */
  GLCD_FillRect(0, 176, SCR_W, 8, C_SKY);
  yq = by << 4;
  rng ^= g_ms * 2654435761u ^ SysTick->VAL;
  if (rng == 0) rng = 1;
  return 1;
}

/* One run. Returns 1 to retry, 0 to go back to the menu */
static int run(void) {
  uint32_t last = g_ms;
  uint8_t  ev, frame = 0, i;
  int      far_x, new_y;
  char     s[24];

  reset_game();
  if (!wait_for_start()) return 0;

  vy = FLAP_V;
  sound_tone(1200, 15);

  for (;;) {
    ev = input_get();
    if (EV_CODE(ev) == EV_BACK) return 0;
    if (ev == EV_UP || ev == EV_INT0) {  /* repeats are ignored: tap to flap */
      vy = FLAP_V;
      wing = 1;
      sound_tone(1200, 15);
    }

    if (g_ms - last < FRAME_MS) continue;
    last += FRAME_MS;
    frame++;

    /* ---- Pipes ------------------------------------------------------- */
    for (i = 0; i < N_PIPES; i++) {
      pipe_step(&pipes[i]);
      if (!pipes[i].scored && pipes[i].x + PIPE_W < BIRD_X) {
        pipes[i].scored = 1;
        score++;
        draw_hud();
        sound_tone(1568, 60);
      }
    }
    for (i = 0; i < N_PIPES; i++) {      /* recycle pipes that left the screen */
      if (pipes[i].x + PIPE_W + SPEED <= 0) {
        int k;
        far_x = 0;
        for (k = 0; k < N_PIPES; k++) if (pipes[k].x > far_x) far_x = pipes[k].x;
        pipes[i].x = far_x + SPACING;
        pipes[i].gap = new_gap();
        pipes[i].scored = 0;
      }
    }

    /* ---- Bird -------------------------------------------------------- */
    vy += GRAVITY;
    if (vy > MAX_V) vy = MAX_V;
    yq += vy;
    if (yq < (PLAY_Y0 << 4)) { yq = PLAY_Y0 << 4; vy = 0; }  /* ceiling */
    new_y = yq >> 4;
    if (new_y + BIRD_H > GROUND_Y) new_y = GROUND_Y - BIRD_H;
    if ((frame & 3) == 0) wing ^= 1;
    draw_bird(new_y);

    /* ---- Crash? ------------------------------------------------------ */
    if (hit_pipe() || by + BIRD_H >= GROUND_Y) break;
  }

  /* ---- Game over ----------------------------------------------------- */
  sound_play(snd_crash, 3);
  for (i = 0; i < 3; i++) {
    LED_Out(0xFF); delay_ms(100);
    LED_Out(0x00); delay_ms(100);
  }
  if (score > best_score) best_score = score;
  draw_hud();

  GLCD_FillRect(36, 80, 248, 76, C_ACCENT);
  GLCD_FillRect(39, 83, 242, 70, C_HEADER);
  ui_text_center(88, 1, "GAME OVER", C_TEXT, C_HEADER);
  sprintf(s, "score %u   best %u", score, best_score);
  ui_text_center(118, 0, s, C_ACCENT, C_HEADER);
  ui_text_center(136, 0, "SELECT: retry   hold SELECT: menu", C_DIM, C_HEADER);

  input_flush();
  for (;;) {
    ev = EV_CODE(input_get());
    if (ev == EV_BACK)   return 0;
    if (ev == EV_SELECT) return 1;
  }
}

void flappy_main(void) {
  build_bird();
  sound_init();
  while (run()) { }
  sound_off();
  LED_Out(0);
}
