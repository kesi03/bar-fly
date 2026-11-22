/* bar-fly: six-operator FM electric piano engine.
 *
 * The engine is audio-flow only. Each voice copies its patch at note-on, so
 * changing the patch (or the FM control knobs) never breaks a ringing note.
 * All shared values arrive in the fm_ctrl snapshot copied by the audio flow.
 */
#ifndef BAR_FLY_FM_H
#define BAR_FLY_FM_H

#include "vibe.h"

#define FM_OPS   6
#define FM_VOICES 12
#define FM_SINE_N 4096

typedef struct {
  int ratio;   /* frequency multiplier x100, e.g. 200 = 2.00 */
  int detune;  /* cents */
  int level;   /* 0..100 */
  int ar;      /* attack ms */
  int dr;      /* decay ms */
  int sl;      /* sustain level x100 */
  int rr;      /* release ms */
  int dest;    /* -1 = output carrier, otherwise operator index to modulate */
  int fb;      /* self feedback x100 */
  int wave;    /* OPL3 waveform ROM: 0 sine, 1 half sine, 2 abs sine, 3 pulse */
} fm_op_def;

/* One OPL3 operator, as raw chip register fields. tl is 6-bit (higher =
 * quieter), envelopes are 4-bit, mult is the ratio register (1 = x1), and
 * sl is inverted: 0 = full-volume sustain, 15 = decay to silence. avekm and
 * ksl are the high bits of the 0x20/0x40 registers from bank data; zero means
 * "not present" for the hand-written recipes, which then use KSR only. */
typedef struct {
  uint8_t mult, tl, ar, dr, sl, rr, wave;
  uint8_t avekm, ksl;        /* raw 0x20 byte, 0x40 key-scale bits */
} opl3_op_recipe;

/* A Nuked OPL3 (YMF262) voice. nch = 1 for a normal 2-op voice, 2 for
 * pseudo-4-op / additive drawbar voices that need four operators per note.
 * ko and fine are the per-voice note offset (semitones) and fine detune
 * (1/32 semitone) stored by DMXOPL/WOPL bank instruments. */
typedef struct {
  int nch;
  opl3_op_recipe op[2][2];   /* [channel][0 = modulator, 1 = carrier] */
  uint8_t fb[2], con[2];     /* feedback 0..7, connection 0 = FM, 1 = additive */
  int8_t ko[2], fine[2];     /* bank note offsets and second-voice detune */
} opl3_patch_def;

typedef struct {
  const char *name;
  int nops;
  fm_op_def op[FM_OPS];
  const opl3_patch_def *opl3;   /* non-NULL = render this patch with Nuked OPL3 */
} fm_patch;

/* Every field except detune is scaled x1000. detune is in cents. */
typedef struct {
  int index;       /* modulator level multiplier */
  int decay;       /* modulator/operator decay time multiplier */
  int feedback;    /* added self feedback */
  int harmonic;    /* upper-partial level multiplier */
  int detune;      /* cents added to every operator */
  int sub;         /* sine sub oscillator level */
  int brightness;  /* master low-pass, 0..1 (applied in FX) */
  int attack;      /* attack time multiplier */
  int release;     /* release time multiplier */
  int velocity;    /* velocity sensitivity */
  int spread;      /* stereo spread */
  int volume;      /* instrument level */
  int vib_depth;   /* vibrato depth */
  int vib_rate;    /* vibrato rate in Hz x1000 */
} fm_ctrl;

/* Control ids published as atomic ints by main.c (all x1000, detune in cents). */
enum {
  FM_C_INDEX = 0, FM_C_DECAY, FM_C_FEEDBACK, FM_C_HARMONIC,
  FM_C_DETUNE, FM_C_SUB, FM_C_BRIGHT, FM_C_ATTACK, FM_C_RELEASE,
  FM_C_VELOCITY, FM_C_SPREAD, FM_C_VOLUME, FM_C_VIB_DEPTH, FM_C_VIB_RATE,
  FM_C_COUNT
};

int barfly_ctrl_atom(int id);   /* implemented in main.c, control + audio safe */

void fm_init(float sample_rate);
void fm_reset(void);
void fm_note_on(int note, int vel);
void fm_note_on_patch(int note, int vel, int patch);  /* demo's clean fixed voice */
void fm_note_off(int note);
void fm_all_off(void);
void fm_set_patch(int index);
int  fm_patch_count(void);
const char *fm_patch_name(int index);
int  fm_voice_count(void);
void fm_render(float *l, float *r, int n, const fm_ctrl *c);

#endif
