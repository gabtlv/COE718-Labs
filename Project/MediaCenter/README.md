# Embedded Media Center

Final project for **COE718 Embedded Systems Design** (Toronto Metropolitan University, Fall 2026). It turns the **Keil MCB1700** board (**NXP LPC1768, ARM Cortex-M3**) into a joystick-driven media center with a photo gallery, a USB speaker for the PC, and two games.

> Shared for reference and portfolio purposes. If you're taking COE718, don't submit any of this as your own work.

## Features

| Mode | What it does |
|---|---|
| **Main menu** | Four items with a highlight bar. Only the items that change are redrawn, and the LED for the highlighted item lights up. |
| **Photo gallery** | Wrap-around carousel with page dots, captions and a 3 second slideshow. Photos are RGB565 C arrays exported from GIMP. |
| **MP3 player** | The board appears to the PC as a **USB Audio Class** speaker. Audio streams in over isochronous USB (DMA) and plays at **32 kHz** through the DAC. The potentiometer sets the volume, SELECT mutes, and the screen and LEDs show a level meter. USB connects when the mode opens and disconnects cleanly when it closes. |
| **Minesweeper** | Three levels (up to 13 x 9 with 22 mines). The first move is always safe, empty areas open with an iterative flood fill, and you can chord on numbers. Includes a timer, best time per level, LED mine counter, and sound and LED effects. |
| **Flappy Bird** | 30 fps game with fixed-point physics, scrolling pipes and an animated pixel-art bird. Only the edges of moving objects are repainted, so animation is flicker-free on the slow SPI LCD. The LEDs show the score. |

## Controls

| Input | Menu | Gallery | MP3 player | Minesweeper | Flappy Bird |
|---|---|---|---|---|---|
| Joystick up / down | Move highlight | | | Move cursor | Up: flap |
| Joystick left / right | | Previous / next photo | | Move cursor | |
| SELECT (short press) | Open | Start / stop slideshow | Mute | Reveal or chord | Retry after a crash |
| SELECT (hold 0.8 s) | | Back to menu | Back to menu | Back to menu | Back to menu |
| INT0 button | | | | Place / remove flag | Flap |
| Potentiometer | | | Volume (counter-clockwise = louder) | | |

## How it works

- **No RTOS.** `main.c` runs a super-loop: the menu returns the chosen mode, that mode runs until the user holds SELECT, then the menu comes back. Each mode sets up and releases its own peripherals, so only one mode at a time owns the USB, DAC or timers.
- **Time base and input.** SysTick ticks every 1 ms. Every 10 ms it polls the joystick and INT0 button. A button counts as changed only after two identical reads (20 ms debounce), and events (press, auto-repeat, short press, long press) go into a ring buffer that the main loop reads.
- **Interrupt priorities.** USB (0) > audio sample timer TIMER0 (1) > game sound timer TIMER1 (2) > SysTick (lowest), so drawing and input never delay audio.
- **Sound effects.** A small note sequencer on TIMER1 drives the DAC with square-wave tones, LFSR white noise and rests, so games never wait for a sound to finish.
- **LCD.** The Keil GLCD driver is extended with `GLCD_FillRect()` and `GLCD_DisplayStringXY()` so screens can be redrawn in small pieces instead of clearing the whole display.

## Files

| File | Purpose |
|---|---|
| `main.c`, `app.h` | Start-up, 1 ms time base, mode state machine, shared UI helpers and colour theme |
| `input.c/.h` | Debounced joystick and INT0 events |
| `menu.c` | Main menu |
| `gallery.c`, `photo*.c` | Photo carousel and the image data |
| `mp3.c` | USB speaker mode (adapted from Keil's `usbdmain.c`) |
| `mines.c` | Minesweeper |
| `flappy.c` | Flappy Bird |
| `sound.c/.h` | TIMER1 + DAC sound effects for the games |
| `config.h` | `LITE_BUILD` switch (see below) |
| `usb*.c/.h`, `adcuser.c/.h`, `audio.h` | Keil USB device stack and audio class code |
| `GLCD_SPI_LPC1700.c`, `GLCD.h`, `Font_*.h`, `LED.c/.h` | Board drivers from Keil's examples (GLCD extended) |
| `build.ps1` | Laptop build script |

## Building

The project is based on Keil's `USBAudio` example and uses its legacy `MediaCenter.uvproj` with the `DMA.sct` scatter file.

**On a lab PC (full build, for the board):**
1. Open `MediaCenter.uvproj` in µVision 5.
2. **Project → Rebuild all target files.**
3. **Flash → Download (F8)**, then press RESET on the MCB1700.

**On a laptop with the free MDK-Lite licence:** MDK-Lite can only link 32 KB, and the photos alone are larger than that. Run `build.ps1`. It switches `config.h` to `LITE_BUILD 1` (a tiny stand-in photo), builds, and switches it back, so the project always stays ready for the full build. The lite build is about 28 KB. The full build is about 208 KB of the LPC1768's 512 KB flash.

## Adding photos

1. In GIMP, scale the image to fit (for example 200 x 150), then **Image → Transform → Flip Vertically** (the LCD driver draws bitmaps bottom-up).
2. **File → Export As** `photoN.c` (C source code). Tick only **Use macros instead of struct** and **Save as RGB565 (16-bit)**.
3. In the exported file, replace `guint8` with `unsigned char`.
4. In `gallery.c`, add `#include "photoN.c"` and one line to the `photos[]` table. Keep captions to 14 characters or fewer. Each photo is included once only, so its data is stored once in flash.

## Status

All modes are written and build with **0 errors and 0 warnings**. The Minesweeper game logic was tested on a PC with a mocked LCD over 1,500 simulated games under AddressSanitizer and UndefinedBehaviorSanitizer. Testing on the board is in progress.

## Credits

The USB device stack, audio class code, LCD driver, fonts, LED driver and device startup files come from Keil's MCB1700 examples (via the course materials) and are subject to Keil's licence. Everything else was written for this project.

## Author

Gab Talavera, Computer Engineering, Toronto Metropolitan University
[GitHub](https://github.com/gabtlv) · [LinkedIn](https://linkedin.com/in/gabtlv) · [Portfolio](https://gabtlv.vercel.app)
