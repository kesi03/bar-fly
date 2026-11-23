/* BAR-FLY -- clean-room synthesized drums for bossa nova.
 * A soft kick, a cross-stick rim, hats and a few auxiliary 808-style voices,
 * plus eight original 16-step grooves. Everything is generated: no samples.
 *
 * Each step is one sixteenth note. The patterns use three tracks:
 *   bit 0 (0x01) kick, bit 1 (0x02) cross-stick/rim, bit 3 (0x08) closed hat.
 * The other five voices are available to the synth but unused by the factory
 * grooves.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "drums.h"

static const char *const PAT_NAMES[BF_DRUM_PATTERNS] = {
  "BASIC BOSSA", "ANTICIPATED", "LOUNGE BAR", "SYNCOPATED",
  "JAZZ CLUB", "HOTEL CASIO", "SAMBA", "LOOSE ENDING"
};

/* 8-bar song form: 1, 2, 3, 4, 1, 5, 7, 8 (zero-based below). */
static const unsigned char SONG_FORM[8] = { 0, 1, 2, 3, 0, 4, 6, 7 };

/* bit 0 kick, 1 rim, 2 clap, 3 hat, 4 open hat, 5 tom, 6 cowbell, 7 perc */
static const unsigned char FACTORY[BF_DRUM_PATTERNS][BF_DRUM_STEPS] = {
  /* 1 BASIC BOSSA -- the core pattern: kick 1 / "and" of 2 / 4, rim 2 and 4. */
  { 0x09,0x00,0x08,0x00, 0x0A,0x00,0x09,0x00, 0x08,0x00,0x08,0x00, 0x0B,0x00,0x08,0x00 },
  /* 2 ANTICIPATED -- the second kick arrives early on the "and" of 1. */
  { 0x09,0x00,0x08,0x00, 0x0A,0x01,0x08,0x00, 0x08,0x00,0x09,0x00, 0x0A,0x00,0x08,0x00 },
  /* 3 LOUNGE BAR -- sparse kick, a two-two hat figure. */
  { 0x09,0x00,0x00,0x08, 0x0A,0x00,0x01,0x08, 0x09,0x00,0x00,0x08, 0x0A,0x00,0x00,0x08 },
  /* 4 SYNCO PATED -- kick pushed to the "and" of 2 and the "and" of 3. */
  { 0x09,0x00,0x08,0x00, 0x0A,0x00,0x08,0x01, 0x08,0x00,0x09,0x00, 0x0A,0x00,0x08,0x00 },
  /* 5 JAZZ CLUB -- an extra answer kick on the last "and". */
  { 0x09,0x00,0x08,0x00, 0x0A,0x01,0x08,0x00, 0x09,0x00,0x08,0x00, 0x0A,0x01,0x08,0x00 },
  /* 6 HOTEL CASIO -- busy, slightly mechanical sixteenth hat. */
  { 0x09,0x08,0x00,0x08, 0x0A,0x09,0x00,0x08, 0x08,0x08,0x00,0x09, 0x0A,0x08,0x00,0x08 },
  /* 7 SAMBA -- a four-on-the-floor kern around the surdo. */
  { 0x09,0x00,0x08,0x09, 0x0A,0x00,0x09,0x00, 0x08,0x01,0x08,0x08, 0x0B,0x00,0x08,0x00 },
  /* 8 LOOSE ENDING -- off-beat kicks and a doubled last hat. */
  { 0x09,0x00,0x08,0x00, 0x0A,0x00,0x09,0x00, 0x08,0x00,0x08,0x01, 0x0A,0x00,0x09,0x08 }
};

typedef struct {
  volatile int on, kind, age;   /* age is the allocation stamp, used for voice stealing */
  int samples;                  /* samples since trigger, used for transients */
  float ph, ph2, env, dec, lp, hp_lp, f0, f1, pan_l, pan_r, vel;
  vd_noise noise;
} bf_dvoice;

static float s_sr;
static bf_dvoice dv[BF_DRUM_VOICES];
static volatile unsigned char steps[BF_DRUM_PATTERNS][BF_DRUM_STEPS];
static int age_ctr;

void bf_drums_init(float sr)
{
  int p, s;
  s_sr = sr > 1000.0f ? sr : 48000.0f;
  for (p = 0; p < BF_DRUM_PATTERNS; p++)
    for (s = 0; s < BF_DRUM_STEPS; s++) steps[p][s] = FACTORY[p][s];
  for (s = 0; s < BF_DRUM_VOICES; s++) { dv[s].on = 0; dv[s].samples = 0; }
  age_ctr = 0;
  VIBE_RELEASE();
}

void bf_drums_reset(void)
{
  int s;
  for (s = 0; s < BF_DRUM_VOICES; s++) dv[s].on = 0;
}

void bf_drum_step_set(int pat, int step, int mask)
{
  if (pat < 0 || pat >= BF_DRUM_PATTERNS) return;
  if (step < 0 || step >= BF_DRUM_STEPS) return;
  steps[pat][step] = (unsigned char)(mask & 255);
  VIBE_RELEASE();
}

void bf_drum_pattern_restore(int pat)
{
  int s;
  if (pat < 0 || pat >= BF_DRUM_PATTERNS) return;
  for (s = 0; s < BF_DRUM_STEPS; s++) steps[pat][s] = FACTORY[pat][s];
  VIBE_RELEASE();
}

unsigned char bf_drum_step(int pat, int step)
{
  unsigned char m;
  if (pat < 0 || pat >= BF_DRUM_PATTERNS) pat = 0;
  if (step < 0 || step >= BF_DRUM_STEPS) step = 0;
  VIBE_ACQUIRE();
  m = steps[pat][step];
  return m;
}

const char *bf_drum_pattern_name(int pat)
{
  if (pat < 0 || pat >= BF_DRUM_PATTERNS) pat = 0;
  return PAT_NAMES[pat];
}

int bf_drum_song_pattern(int bar)
{
  bar = ((bar % 8) + 8) % 8;
  return SONG_FORM[bar];
}

int bf_drum_voice_count(void)
{
  int i, n = 0;
  for (i = 0; i < BF_DRUM_VOICES; i++) if (dv[i].on) n++;
  return n;
}

void bf_drums_trigger(int voice, int vel)
{
  bf_dvoice *v = 0;
  int i, best = -1, oldest = -1;
  float amp = vd_clamp((float)vel / 127.0f, 0.05f, 1.0f);

  if (voice < 0 || voice >= BF_D_COUNT) voice = BF_D_KICK;
  for (i = 0; i < BF_DRUM_VOICES; i++) {
    if (!dv[i].on) { best = i; break; }
    if (dv[i].age > oldest) { oldest = dv[i].age; best = i; }
  }
  if (best < 0) return;
  v = &dv[best];
  v->on = 1;
  v->kind = voice;
  v->age = ++age_ctr;
  v->samples = 0;
  v->env = 1.0f;
  v->vel = amp;
  v->ph = 0.0f;
  v->ph2 = 0.0f;
  v->lp = 0.0f;
  v->hp_lp = 0.0f;
  v->noise.s = 0x9E3779B9u + (uint32_t)(age_ctr * 2654435761u);
  switch (voice) {
  case BF_D_KICK:   v->f0 = 140.f; v->f1 = 52.f;  v->dec = 0.30f; v->pan_l = v->pan_r = .72f; break;
  case BF_D_RIM:    v->f0 = 430.f; v->f1 = 300.f; v->dec = 0.055f; v->pan_l = .76f; v->pan_r = .64f; break;
  case BF_D_CLAP:   v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.22f; v->pan_l = .66f; v->pan_r = .80f; break;
  case BF_D_HAT:    v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.045f; v->pan_l = .82f; v->pan_r = .58f; break;
  case BF_D_OPEN:   v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.38f; v->pan_l = .58f; v->pan_r = .82f; break;
  case BF_D_TOM:    v->f0 = 190.f; v->f1 = 100.f; v->dec = 0.26f; v->pan_l = v->pan_r = .70f; break;
  case BF_D_COW:    v->f0 = 540.f; v->f1 = 800.f; v->dec = 0.20f; v->pan_l = .68f; v->pan_r = .72f; break;
  default:          v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.08f; v->pan_l = .72f; v->pan_r = .68f; break;
  }
}

void bf_drums_render(float *l, float *r, int n, const bf_drum_ctrl *c)
{
  int i, vi;
  float gain = c ? c->gain : 0.8f;
  float tune = c ? vd_clamp(c->tune, 0.5f, 1.8f) : 1.0f;
  float decs = c ? vd_clamp(c->decay, 0.25f, 2.5f) : 1.0f;
  float tone = c ? vd_clamp(c->tone, 0.0f, 1.0f) : 0.5f;

  for (i = 0; i < n; i++) {
    float out_l = 0.0f, out_r = 0.0f;
    for (vi = 0; vi < BF_DRUM_VOICES; vi++) {
      bf_dvoice *v = &dv[vi];
      float s = 0.0f, env;
      if (!v->on) continue;
      v->samples++;
      env = v->env;
      if (env < 0.0006f) { v->on = 0; continue; }

      if (v->kind == BF_D_KICK || v->kind == BF_D_TOM) {
        float f = (v->f1 + (v->f0 - v->f1) * env * env) * tune;
        v->ph += vd_clamp(f, 25.f, 1000.f) / s_sr;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        s = vd_sin(v->ph) * env;
        if (v->kind == BF_D_KICK && v->samples < (int)(s_sr * 0.004f))
          s += vd_noise_next(&v->noise) * 0.35f *
               (1.0f - (float)v->samples / (s_sr * 0.004f));
      } else if (v->kind == BF_D_RIM) {
        /* cross-stick: short woody tok with a bright transient click */
        float f = (v->f1 + (v->f0 - v->f1) * env * env) * tune;
        float nz = vd_noise_next(&v->noise);
        float click = v->samples < (int)(s_sr * 0.0015f) ? 0.45f : 0.0f;
        v->ph += vd_clamp(f, 80.f, 1200.f) / s_sr;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        v->hp_lp += 0.55f * (nz - v->hp_lp);
        s = (0.75f * vd_sin(v->ph) + 0.20f * (nz - v->hp_lp) + click * nz) * env;
      } else if (v->kind == BF_D_COW) {
        float dt1 = v->f0 * tune / s_sr, dt2 = v->f1 * tune / s_sr;
        v->ph += dt1; v->ph2 += dt2;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
        v->lp += 0.30f * (vd_square(v->ph, dt1) + vd_square(v->ph2, dt2) - v->lp);
        s = v->lp * env * 0.45f;
      } else {
        float nz = vd_noise_next(&v->noise);
        int clap = v->kind == BF_D_CLAP;
        int open = v->kind == BF_D_OPEN;
        int gate = 1;
        if (clap) {
          int a = v->samples;
          int b1 = (int)(s_sr * 0.010f), b2 = (int)(s_sr * 0.021f);
          gate = (a < (int)(s_sr * 0.002f) || (a >= b1 && a < b1 + (int)(s_sr * 0.002f)) ||
                  (a >= b2 && a < b2 + (int)(s_sr * 0.002f)) || a > b2) ? 1 : 0;
          env *= gate ? 1.0f : 0.55f;
        }
        v->hp_lp += (open ? 0.28f : (0.25f + 0.45f * tone)) * (nz - v->hp_lp);
        s = (nz - v->hp_lp) * env * (clap ? 0.75f : 0.60f);
      }

      s *= v->vel;
      out_l += s * v->pan_l;
      out_r += s * v->pan_r;
      v->env *= vd_coef(v->dec / decs / 5.0f, s_sr);
    }
    l[i] = out_l * gain;
    r[i] = out_r * gain;
  }
}
