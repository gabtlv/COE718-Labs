/*----------------------------------------------------------------------------
 * input.c - debounced joystick + INT0 button events
 *
 *   Polled every 10 ms from the SysTick handler. A button counts as changed
 *   only after it reads the same for two polls in a row (20 ms debounce).
 *   Events go into a small ring buffer that the main loop drains.
 *
 *   Joystick (MCB1700, active low): P1.20 centre, P1.23 up, P1.24 right,
 *                                   P1.25 down,   P1.26 left
 *   INT0 push button (active low):  P2.10
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include "LPC17xx.h"
#include "input.h"

/* Button bits (our own numbering) */
#define B_SEL    (1u << 0)
#define B_UP     (1u << 1)
#define B_DOWN   (1u << 2)
#define B_LEFT   (1u << 3)
#define B_RIGHT  (1u << 4)
#define B_INT0   (1u << 5)

#define POLL_MS        10
#define REPEAT_DELAY   40     /* polls (400 ms) before a held direction repeats */
#define REPEAT_RATE    12     /* polls (120 ms) between repeats                 */
#define BACK_POLLS     (BACK_HOLD_MS / POLL_MS)

#define QSIZE 16
static volatile uint8_t q[QSIZE];
static volatile uint8_t q_head, q_tail;

static uint8_t  raw_prev, stable;
static uint16_t dir_held;         /* polls the current direction has been held */
static uint16_t sel_held;         /* polls SELECT has been held                */
static uint8_t  sel_used;         /* SELECT hold already produced EV_BACK      */

static void push(uint8_t ev) {
  uint8_t next = (q_head + 1) & (QSIZE - 1);
  if (next != q_tail) {           /* drop the event if the queue is full       */
    q[q_head] = ev;
    q_head = next;
  }
}

static uint8_t read_raw(void) {
  uint32_t p1 = ~LPC_GPIO1->FIOPIN;   /* invert: pressed reads as 1 */
  uint32_t p2 = ~LPC_GPIO2->FIOPIN;
  uint8_t  b  = 0;

  if (p1 & (1u << 20)) b |= B_SEL;
  if (p1 & (1u << 23)) b |= B_UP;
  if (p1 & (1u << 24)) b |= B_RIGHT;
  if (p1 & (1u << 25)) b |= B_DOWN;
  if (p1 & (1u << 26)) b |= B_LEFT;
  if (p2 & (1u << 10)) b |= B_INT0;
  return b;
}

void input_init(void) {
  LPC_SC->PCONP |= (1 << 15);                          /* GPIO power        */
  LPC_PINCON->PINSEL3 &= ~((3u << 8) | (3u << 14) | (3u << 16) | (3u << 18) | (3u << 20));
  LPC_GPIO1->FIODIR   &= ~((1u << 20) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 26));
  LPC_PINCON->PINSEL4 &= ~(3u << 20);                  /* P2.10 as GPIO     */
  LPC_GPIO2->FIODIR   &= ~(1u << 10);

  raw_prev = stable = read_raw();
  q_head = q_tail = 0;
}

void input_poll(void) {
  uint8_t raw = read_raw();
  uint8_t pressed, released, dir;

  if (raw != raw_prev) {           /* still bouncing - wait for it to settle */
    raw_prev = raw;
    return;
  }
  pressed  = raw & ~stable;
  released = stable & ~raw;
  stable   = raw;

  /* ---- Directions: event on press, then auto-repeat while held ---------- */
  if (pressed & B_UP)    push(EV_UP);
  if (pressed & B_DOWN)  push(EV_DOWN);
  if (pressed & B_LEFT)  push(EV_LEFT);
  if (pressed & B_RIGHT) push(EV_RIGHT);

  dir = stable & (B_UP | B_DOWN | B_LEFT | B_RIGHT);
  if (dir && !(pressed & dir)) {
    dir_held++;
    if (dir_held >= REPEAT_DELAY && ((dir_held - REPEAT_DELAY) % REPEAT_RATE) == 0) {
      if (dir & B_UP)    push(EV_UP    | EV_REPEAT);
      if (dir & B_DOWN)  push(EV_DOWN  | EV_REPEAT);
      if (dir & B_LEFT)  push(EV_LEFT  | EV_REPEAT);
      if (dir & B_RIGHT) push(EV_RIGHT | EV_REPEAT);
    }
  } else {
    dir_held = 0;
  }

  /* ---- SELECT: short press acts on release, long hold means BACK -------- */
  if (pressed & B_SEL) {
    sel_held = 0;
    sel_used = 0;
  }
  if (stable & B_SEL) {
    if (sel_held < 0xFFFF) sel_held++;
    if (!sel_used && sel_held >= BACK_POLLS) {
      sel_used = 1;
      push(EV_BACK);
    }
  }
  if (released & B_SEL) {
    if (!sel_used) push(EV_SELECT);
    sel_used = 0;
  }

  /* ---- INT0 button ------------------------------------------------------- */
  if (pressed & B_INT0) push(EV_INT0);
}

uint8_t input_get(void) {
  uint8_t ev;
  if (q_tail == q_head) return EV_NONE;
  ev = q[q_tail];
  q_tail = (q_tail + 1) & (QSIZE - 1);
  return ev;
}

void input_flush(void) {
  q_tail = q_head;
}

uint8_t input_select_down(void) {
  return (stable & B_SEL) ? 1 : 0;
}
