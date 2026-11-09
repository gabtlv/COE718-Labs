/*----------------------------------------------------------------------------
 * sound.h - simple sound effects for the games (TIMER1 + DAC)
 *   Gab Talavera, 501239810
 *----------------------------------------------------------------------------*/
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#define NOTE_REST   0       /* silence for the note's duration              */
#define NOTE_NOISE  1       /* white noise (explosions, crashes)            */

typedef struct {
  uint16_t freq;            /* Hz, or NOTE_REST / NOTE_NOISE                */
  uint16_t ms;              /* duration                                     */
} note_t;

void sound_init(void);                          /* call when a game starts  */
void sound_off(void);                           /* call when a game exits   */
void sound_play(const note_t *seq, uint8_t n);  /* start a sequence         */
void sound_tone(uint16_t freq, uint16_t ms);    /* one tone                 */

#endif
