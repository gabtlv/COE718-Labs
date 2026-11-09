/*----------------------------------------------------------------------------
 * menu.c - main menu
 *
 *   UP/DOWN moves the highlight (wraps around), SELECT opens the item.
 *   The LED matching the highlighted item is lit. Only the two items that
 *   change are redrawn on each move, so the menu responds instantly.
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include "app.h"
#include "input.h"
#include "GLCD.h"
#include "LED.h"

#define N_ITEMS   4
#define ITEM_X    20
#define ITEM_W    280
#define ITEM_H    34
#define ITEM_Y0   54
#define ITEM_GAP  8

static const char *const labels[N_ITEMS] = {
  "Photo Gallery", "MP3 Player", "Minesweeper", "Flappy Bird"
};
static const app_mode_t modes[N_ITEMS] = {
  MODE_GALLERY, MODE_MP3, MODE_MINES, MODE_FLAPPY
};
/* A different tag colour per item */
static const unsigned short tags[N_ITEMS] = {
  RGB565( 72, 200, 255),   /* gallery: sky blue  */
  RGB565(255,  96, 160),   /* mp3: pink          */
  RGB565(120, 230, 120),   /* mines: green       */
  RGB565(255, 220,  64)    /* flappy: yellow     */
};

static uint8_t sel = 0;    /* remembered between visits */

static void draw_item(uint8_t i, uint8_t on) {
  unsigned int y  = ITEM_Y0 + i * (ITEM_H + ITEM_GAP);
  unsigned short bg = on ? C_ACCENT : C_ITEM;
  unsigned short fg = on ? C_DARK   : C_TEXT;

  GLCD_FillRect(ITEM_X, y, ITEM_W, ITEM_H, bg);
  GLCD_FillRect(ITEM_X, y, 8, ITEM_H, tags[i]);
  GLCD_SetTextColor(fg);
  GLCD_SetBackColor(bg);
  GLCD_DisplayStringXY(ITEM_X + 24, y + 5, 1, (unsigned char *)labels[i]);
  if (on) {
    GLCD_DisplayStringXY(ITEM_X + ITEM_W - 28, y + 5, 1, (unsigned char *)">");
  }
}

app_mode_t menu_main(void) {
  uint8_t i, ev, old;

  ui_header("MEDIA CENTER");
  for (i = 0; i < N_ITEMS; i++) draw_item(i, i == sel);
  ui_footer("UP/DOWN: choose   SELECT: open   hold SELECT: back");
  LED_Out(1u << sel);
  input_flush();

  for (;;) {
    ev = EV_CODE(input_get());
    if (ev == EV_UP || ev == EV_DOWN) {
      old = sel;
      if (ev == EV_UP) sel = (sel == 0) ? N_ITEMS - 1 : sel - 1;
      else             sel = (sel + 1) % N_ITEMS;
      draw_item(old, 0);
      draw_item(sel, 1);
      LED_Out(1u << sel);
    } else if (ev == EV_SELECT) {
      return modes[sel];
    }
  }
}
