/* bar-fly post-FX rack. All amounts are x1000 ints published by main.c.
 *
 * Chain: saturation -> compressor -> 3-band EQ -> auto-wah -> ring modulator
 *        -> phaser -> stereo chorus -> flanger -> tremolo / auto-pan -> Leslie
 *        -> stereo/tape delay -> room / hall / plate / spring reverb
 *        -> bitcrusher -> tone -> limiter -> master
 *
 * Audio flow only. No heap, no libc, no host calls. The Leslie is a clean-room
 * dual-rotor model: separate horn/drum motors, geometric Doppler and mic mix.
 */
#ifndef BAR_FLY_FX_H
#define BAR_FLY_FX_H

#include "vibe.h"

enum {
  FX_SAT_TYPE = 0, FX_DRIVE,
  FX_COMP_ON, FX_COMP_AMT,
  FX_EQ_BASS, FX_EQ_MID, FX_EQ_TREBLE, FX_TONE,
  FX_PHASER_MIX, FX_PHASER_RATE, FX_PHASER_DEPTH,
  FX_CHORUS_MIX, FX_CHORUS_RATE, FX_CHORUS_DEPTH,
  FX_TREM_DEPTH, FX_TREM_RATE, FX_TREM_SHAPE,
  FX_PAN_DEPTH, FX_PAN_RATE,
  FX_ROTARY_ON, FX_ROTARY_SPEED, FX_ROTARY_DEPTH, FX_ROTARY_DRIVE,
  FX_ROTARY_BALANCE,
  FX_DELAY_TYPE, FX_DELAY_MIX, FX_DELAY_MS, FX_DELAY_FB,
  FX_REVERB_TYPE, FX_REVERB_MIX, FX_REVERB_DECAY, FX_REVERB_DAMP,
  FX_FLANGER_MIX, FX_FLANGER_RATE, FX_FLANGER_DEPTH,
  FX_WAH_MIX, FX_WAH_DEPTH,
  FX_RING_MIX, FX_RING_FREQ, FX_RING_DEPTH,
  FX_CRUSH_MIX, FX_CRUSH_BITS, FX_CRUSH_RATE,
  FX_MASTER,
  FX_COUNT
};

/* 0 off, 1 solid state, 2 tube. Delay: 0 off, 1 ping-pong, 2 tape.
 * Reverb: 0 off, 1 room, 2 hall, 3 plate, 4 spring.
 * Rotary speed: 0 stop, 1 slow, 2 fast. Trem shape: 0 sine, 1 tri, 2 square. */
void fx_init(float sample_rate);
void fx_reset(void);
void fx_process(float *l, float *r, int n, const int *amounts);
int  fx_rotor_hz100(void);

#endif
