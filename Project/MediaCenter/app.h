/*----------------------------------------------------------------------------
 * app.h - shared definitions for the COE 718 Media Center
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#ifndef APP_H
#define APP_H

#include <stdint.h>

/* ---- Screen layout ------------------------------------------------------- */
#define SCR_W        320
#define SCR_H        240

/* ---- Colour theme (RGB565) ----------------------------------------------- */
#define RGB565(r,g,b) ((unsigned short)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_BG         RGB565( 18,  20,  32)   /* near-black navy background   */
#define C_HEADER     RGB565( 32,  44,  92)   /* header bar                   */
#define C_ACCENT     RGB565(255, 176,  32)   /* amber highlight              */
#define C_ITEM       RGB565( 40,  44,  64)   /* unselected menu item         */
#define C_TEXT       RGB565(240, 240, 240)   /* main text                    */
#define C_DIM        RGB565(150, 156, 176)   /* hint / secondary text        */
#define C_DARK       RGB565(  0,   0,   0)

/* ---- Millisecond time base (SysTick, main.c) ----------------------------- */
extern volatile uint32_t g_ms;               /* ms since reset                */
void delay_ms(uint32_t ms);

/* ---- Shared drawing helpers (main.c) ------------------------------------- */
void ui_header(const char *title);           /* clear screen + title bar      */
void ui_footer(const char *hint);            /* one-line hint at the bottom   */
void ui_text_center(unsigned int y, unsigned char font, const char *s,
                    unsigned short fg, unsigned short bg);

/* ---- Modes ---------------------------------------------------------------- */
typedef enum {
  MODE_MENU = 0,
  MODE_GALLERY,
  MODE_MP3,
  MODE_MINES,
  MODE_FLAPPY,
  MODE_COUNT
} app_mode_t;

app_mode_t menu_main(void);       /* menu.c    - returns the chosen mode           */
void   gallery_main(void);    /* gallery.c - runs until the user goes back     */
void   mp3_main(void);        /* mp3.c                                         */
void   mines_main(void);      /* mines.c                                       */
void   flappy_main(void);     /* flappy.c                                      */

#endif
