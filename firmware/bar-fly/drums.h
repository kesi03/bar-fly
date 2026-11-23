/* BAR-FLY -- synthesized bossa drum voices and eight 16-step grooves.
 * Control flow reads pattern steps and LEDs; the audio flow triggers and renders
 * the voices. No host calls, no allocation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BF_DRUMS_H
#define BF_DRUMS_H

#include "vibe.h"
#include "vibe_dsp.h"

#define BF_DRUM_VOICES 12
#define BF_DRUM_TRACKS 8
#define BF_DRUM_STEPS 16
#define BF_DRUM_PATTERNS 8

enum {
  BF_D_KICK = 0, BF_D_RIM, BF_D_CLAP, BF_D_HAT,
  BF_D_OPEN, BF_D_TOM, BF_D_COW, BF_D_PERC,
  BF_D_COUNT
};

typedef struct {
  float gain, tune, decay, tone;
} bf_drum_ctrl;

void  bf_drums_init(float sr);
void  bf_drums_reset(void);
void  bf_drums_trigger(int voice, int vel);       /* audio flow only */
void  bf_drums_render(float *l, float *r, int n, const bf_drum_ctrl *c);
unsigned char bf_drum_step(int pat, int step);    /* bit mask of BF_D_* tracks */
void  bf_drum_step_set(int pat, int step, int mask);
void  bf_drum_pattern_restore(int pat);
const char *bf_drum_pattern_name(int pat);
int   bf_drum_song_pattern(int bar);    /* 8-bar song form, zero-based bar */
int   bf_drum_voice_count(void);

#endif
