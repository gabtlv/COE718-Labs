/*----------------------------------------------------------------------------
 * gallery.c - photo gallery (carousel)
 *
 *   LEFT/RIGHT: previous/next photo (wraps around at both ends)
 *   SELECT:     start/stop the slideshow (advances every SLIDE_MS)
 *   hold SELECT: back to the menu
 *
 *   Photos are GIMP "C source" exports (RGB565, macros, flipped vertically,
 *   guint8 changed to unsigned char). Each one is #included exactly ONCE,
 *   here, so its pixel data is only stored once in flash. The files sit in
 *   the "Resources" group of the project as documents (not compiled
 *   separately).
 *
 *   TO ADD A PHOTO: export it from GIMP as photoN.c, add an #include line
 *   below and one line to the photos[] table.
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "input.h"
#include "GLCD.h"
#include "LED.h"

#include "config.h"

#if LITE_BUILD
#include "photo_lite.c"          /* tiny stand-in: fits the 32 KB laptop limit */
#else
#include "photo1.c"
#include "photo2.c"
#include "photo3.c"
#endif

typedef struct {
  unsigned char *px;      /* RGB565 pixel data (rows stored bottom-up)    */
  uint16_t       w, h;
  const char    *name;
} photo_t;

static const photo_t photos[] = {
#if LITE_BUILD
  { PHOTO_LITE_PIXEL_DATA, PHOTO_LITE_WIDTH, PHOTO_LITE_HEIGHT, "Lite 1" },
  { PHOTO_LITE_PIXEL_DATA, PHOTO_LITE_WIDTH, PHOTO_LITE_HEIGHT, "Lite 2" },
  { PHOTO_LITE_PIXEL_DATA, PHOTO_LITE_WIDTH, PHOTO_LITE_HEIGHT, "Lite 3" },
#else
  { PHOTO1_PIXEL_DATA, PHOTO1_WIDTH, PHOTO1_HEIGHT, "Placeholder 1" },
  { PHOTO2_PIXEL_DATA, PHOTO2_WIDTH, PHOTO2_HEIGHT, "Placeholder 2" },
  { PHOTO3_PIXEL_DATA, PHOTO3_WIDTH, PHOTO3_HEIGHT, "Placeholder 3" },
#endif
};
#define N_PHOTOS  ((int)(sizeof(photos) / sizeof(photos[0])))

#define VIEW_H     216                    /* photo area above the caption bar */
#define CAP_Y      VIEW_H
#define CAP_H      (SCR_H - VIEW_H)
#define C_CAPTION  RGB565(32, 36, 52)
#define SLIDE_MS   3000

static int     cur;                       /* index of the photo on screen     */
static uint8_t playing;                   /* slideshow running                */

static void draw_dots(void) {
  int i;
  unsigned int x0 = (SCR_W - (N_PHOTOS * 14 - 8)) / 2;
  for (i = 0; i < N_PHOTOS; i++) {
    GLCD_FillRect(x0 + i * 14, VIEW_H - 12, 6, 6, (i == cur) ? C_ACCENT : C_DIM);
  }
}

static void draw_caption(void) {
  char buf[12];

  GLCD_FillRect(0, CAP_Y, SCR_W, CAP_H, C_CAPTION);
  GLCD_SetBackColor(C_CAPTION);
  GLCD_SetTextColor(C_TEXT);
  GLCD_DisplayStringXY(8, CAP_Y, 1, (unsigned char *)photos[cur].name);

  sprintf(buf, "%d/%d", cur + 1, N_PHOTOS);
  GLCD_SetTextColor(playing ? C_ACCENT : C_DIM);
  GLCD_DisplayStringXY(SCR_W - 8 - 16 * strlen(buf), CAP_Y, 1, (unsigned char *)buf);
}

static void draw_photo(int prev) {
  const photo_t *p = &photos[cur];
  unsigned int x = (SCR_W - p->w) / 2;
  unsigned int y = (VIEW_H - 16 - p->h) / 2;      /* leave room for the dots */

  /* Only clear the photo area if the new photo does not cover the old one */
  if (prev < 0 || photos[prev].w > p->w || photos[prev].h > p->h) {
    GLCD_FillRect(0, 0, SCR_W, VIEW_H - 16, C_DARK);
    GLCD_SetBackColor(C_DARK);
    GLCD_SetTextColor(C_DIM);
    GLCD_DisplayStringXY(2,           (VIEW_H - 16) / 2 - 12, 1, (unsigned char *)"<");
    GLCD_DisplayStringXY(SCR_W - 18,  (VIEW_H - 16) / 2 - 12, 1, (unsigned char *)">");
  }
  GLCD_Bitmap(x, y, p->w, p->h, p->px);
  draw_dots();
  draw_caption();
  LED_Out(1u << (cur % 8));
}

static void show(int idx) {
  int prev = cur;
  cur = (idx + N_PHOTOS) % N_PHOTOS;
  draw_photo(prev);
}

static void gallery_enter(void) {
  GLCD_Clear(C_DARK);
  GLCD_FillRect(0, VIEW_H - 16, SCR_W, 16, C_DARK);
  playing = 0;
  draw_photo(-1);
}

static void gallery_exit(void) {
  playing = 0;
  LED_Out(0);
}

void gallery_main(void) {
  uint8_t  ev;
  uint32_t last_change;

  gallery_enter();
  last_change = g_ms;

  for (;;) {
    ev = EV_CODE(input_get());

    if (ev == EV_BACK) break;

    if (ev == EV_RIGHT) {
      show(cur + 1);
      last_change = g_ms;
    } else if (ev == EV_LEFT) {
      show(cur - 1);
      last_change = g_ms;
    } else if (ev == EV_SELECT) {
      playing = !playing;
      draw_caption();
      last_change = g_ms;
    }

    if (playing && (g_ms - last_change) >= SLIDE_MS) {
      show(cur + 1);
      last_change = g_ms;
    }
  }

  gallery_exit();
}
