/*----------------------------------------------------------------------------
 * sound.c - simple sound effects for the games (TIMER1 + DAC)
 *
 *   The TIMER1 interrupt drives the DAC (P0.26 -> on-board speaker).
 *   A "song" is a short list of notes; the interrupt steps through it on
 *   its own, so the games never wait for a sound to finish.
 *     square tone : interrupt at 2 x freq, output flips high/low each time
 *     noise       : interrupt at 8 kHz, output = random level
 *     rest        : interrupt at 1 kHz, output stays at the middle level
 *
 *   Only the games use this. The MP3 player uses TIMER0 + DAC instead,
 *   and each mode releases its timer when it exits.
 *
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#include "LPC17xx.h"
#include "sound.h"

extern uint32_t SystemFrequency;

#define DAC_MID   0x8000
#define DAC_AMP   0x2800           /* loudness of the effects               */
#define MAX_NOTES 8

static note_t            seq[MAX_NOTES];
static volatile uint8_t  seq_len, seq_pos;
static volatile uint32_t remaining;   /* interrupts left in the current note */
static volatile uint8_t  kind;        /* 0 rest, 1 noise, 2 square           */
static volatile uint8_t  level_hi;
static uint32_t          pclk1;
static uint32_t          lfsr = 0xACE1u;

static void timer_rate(uint32_t hz) {
  LPC_TIM1->TCR = 2;                     /* reset counter                    */
  LPC_TIM1->MR0 = pclk1 / hz - 1;
  LPC_TIM1->TCR = 1;                     /* run                              */
}

/* Load note seq[seq_pos]; returns 0 when the sequence is finished */
static uint8_t load_note(void) {
  uint32_t hz;
  const note_t *n;

  if (seq_pos >= seq_len) return 0;
  n = &seq[seq_pos++];
  if (n->freq == NOTE_REST)       { kind = 0; hz = 1000; }
  else if (n->freq == NOTE_NOISE) { kind = 1; hz = 8000; }
  else                            { kind = 2; hz = 2u * n->freq; }
  remaining = hz * n->ms / 1000;
  if (remaining == 0) remaining = 1;
  timer_rate(hz);
  return 1;
}

void TIMER1_IRQHandler(void) {
  uint32_t out = DAC_MID;

  LPC_TIM1->IR = 1;

  if (remaining == 0) {
    if (!load_note()) {                  /* sequence done: stop the timer    */
      LPC_TIM1->TCR = 0;
      LPC_DAC->CR = DAC_MID;
      return;
    }
  }
  remaining--;

  if (kind == 2) {
    level_hi = !level_hi;
    out = level_hi ? (DAC_MID + DAC_AMP) : (DAC_MID - DAC_AMP);
  } else if (kind == 1) {
    lfsr = (lfsr >> 1) ^ (-(int32_t)(lfsr & 1u) & 0xB400u);   /* 16-bit LFSR */
    out = DAC_MID - DAC_AMP + ((lfsr & 0xFFu) * (2u * DAC_AMP) / 256u);
  }
  LPC_DAC->CR = out & 0xFFC0;
}

void sound_init(void) {
  uint32_t div;

  LPC_PINCON->PINSEL1 &= ~(0x03 << 20);
  LPC_PINCON->PINSEL1 |=  (0x02 << 20);  /* P0.26 = AOUT                    */
  LPC_DAC->CR = DAC_MID;

  div = (LPC_SC->PCLKSEL0 >> 4) & 0x03;  /* TIMER1 peripheral clock         */
  switch (div) {
    case 0x01: pclk1 = SystemFrequency;     break;
    case 0x02: pclk1 = SystemFrequency / 2; break;
    case 0x03: pclk1 = SystemFrequency / 8; break;
    default:   pclk1 = SystemFrequency / 4; break;
  }
  LPC_SC->PCONP |= (1 << 2);             /* power TIMER1                    */
  LPC_TIM1->TCR = 0;
  LPC_TIM1->MCR = 3;                     /* interrupt + reset on MR0        */
  LPC_TIM1->IR  = 1;
  seq_len = seq_pos = 0;
  remaining = 0;
  NVIC_SetPriority(TIMER1_IRQn, 2);
  NVIC_EnableIRQ(TIMER1_IRQn);
}

void sound_off(void) {
  NVIC_DisableIRQ(TIMER1_IRQn);
  LPC_TIM1->TCR = 0;
  LPC_TIM1->IR  = 1;
  LPC_DAC->CR = DAC_MID;
}

void sound_play(const note_t *s, uint8_t n) {
  uint8_t i;
  if (n > MAX_NOTES) n = MAX_NOTES;
  NVIC_DisableIRQ(TIMER1_IRQn);          /* replace whatever is playing     */
  LPC_TIM1->TCR = 0;
  for (i = 0; i < n; i++) seq[i] = s[i];
  seq_len = n;
  seq_pos = 0;
  remaining = 0;
  if (load_note()) {
    NVIC_EnableIRQ(TIMER1_IRQn);
  }
}

void sound_tone(uint16_t freq, uint16_t ms) {
  note_t n;
  n.freq = freq;
  n.ms   = ms;
  sound_play(&n, 1);
}
