#include "vibe_dsp.h"
#include "fx.h"

#define CHORUS_LEN   2048
#define FLANGER_LEN  1024
#define DELAY_MAX    26000
#define REV_COMBS    6
#define REV_APS      2
#define REV_C_MAX    2600
#define REV_AP_MAX   800
#define ROTOR_LEN    4096
#define LES_TAPS     3

/* Clean-room rotary geometry, in metres and metres/second. */
#define LES_HORN_R   0.19f
#define LES_DRUM_R   0.22f
#define LES_MIC_X    0.24f
#define LES_MIC_Y    0.42f
#define LES_C        343.0f

static float fx_sr = 48000.0f;
static float fx_ratio = 1.0f;

/* --- saturation / compressor / EQ / tone ---------------------------------- */
static float sat_dc[2], comp_env, comp_gain;
typedef struct { float b0, b1, b2, a1, a2; } bq;
static bq eq_b[2][3];
static float eq_z[2][3][2];
static vd_svf tone_f[2];
static vd_svf_c tone_c;

/* --- phaser --------------------------------------------------------------- */
static float ph_buf[2][4][512];
static int ph_pos[2];
static float ph_phase;

/* --- chorus --------------------------------------------------------------- */
static float ch_buf[2][CHORUS_LEN];
static int ch_pos[2];
static float ch_phase;

/* --- tremolo / auto-pan --------------------------------------------------- */
static float trem_phase, pan_phase;

/* --- flanger: short modulated delay with gentle regeneration -------------- */
static float fl_buf[2][FLANGER_LEN];
static int fl_pos[2];
static float fl_phase;

/* --- auto-wah / ring modulator / bitcrusher ------------------------------- */
static vd_svf wah_f[2];
static vd_svf_c wah_c;
static float wah_env, wah_cut;
static float ring_phase, ring_inc;
static float crush_hold[2], crush_levels;
static int crush_count, crush_div;

/* --- Leslie: clean dual-rotor rotary speaker ------------------------------ */
typedef struct { float phase, hz, target_hz, coef; } les_rotor;
static vd_svf les_split;
static vd_svf_c les_split_c;
static float les_hbuf[ROTOR_LEN], les_dbuf[ROTOR_LEN];
static int les_w;
static les_rotor les_horn, les_drum;
static float les_horn_lp, les_drum_tone;
static float les_horn_hp_coef, les_drum_tone_coef;
static float les_base_delay, les_dist_scale, les_center_d;
static float rotary_wet, rotary_wet_coef;
static int rotor_hz100;

/* Three taps spread across each rotor's baffle/reflection pattern. */
static const float les_tap_sin[LES_TAPS] = { -0.482f, 0.0f, 0.482f };
static const float les_tap_cos[LES_TAPS] = {  0.876f, 1.0f, 0.876f };
static const float les_tap_gain[LES_TAPS] = {  0.22f, 0.56f, 0.22f };

/* --- delay ---------------------------------------------------------------- */
static float dl_buf[2][DELAY_MAX];
static int dl_pos[2], dl_len;
static float dl_lp[2], dl_wow, dl_wow_phase;
static float fx_process_fb, fx_process_damp;

/* --- reverb --------------------------------------------------------------- */
static const int rev_room[REV_COMBS]  = { 673, 731, 797, 863, 937, 1009 };
static const int rev_hall[REV_COMBS]  = { 1553, 1697, 1847, 1999, 2141, 2287 };
static const int rev_plate[REV_COMBS] = { 1193, 1277, 1361, 1453, 1543, 1637 };
static const int rev_spring[REV_COMBS]= { 397, 421, 449, 479, 509, 541 };
static const int rev_ap_base[REV_APS] = { 556, 441 };
static int rev_cl[REV_COMBS], rev_al[REV_APS];
static float rev_cb[2][REV_COMBS][REV_C_MAX];
static float rev_clp[2][REV_COMBS];
static int rev_cp[2][REV_COMBS];
static float rev_ab[2][REV_APS][REV_AP_MAX];
static int rev_ap[2][REV_APS];
static float rev_phase;

static float q1000(int v) { return (float)v * 0.001f; }
static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void bq_norm(bq *c, float a0)
{
  c->b0 /= a0; c->b1 /= a0; c->b2 /= a0; c->a1 /= a0; c->a2 /= a0;
}

static void bq_lowshelf(bq *c, float hz, float db, float sr)
{
  float w = hz / sr;                   /* turns: w0 = 2*pi*w */
  float sn = vd_sin(w), cs = vd_sin(w + 0.25f);
  float A = vd_exp2(db * 0.0830192f);
  float rt = vd_sqrt(A);
  float al = 1.4142f * rt * sn;        /* 2*sqrt(A)*alpha, shelf S = 1 */
  c->b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + al);
  c->b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
  c->b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - al);
  c->a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
  c->a2 = (A + 1.0f) + (A - 1.0f) * cs - al;
  bq_norm(c, (A + 1.0f) + (A - 1.0f) * cs + al);
}

static void bq_peak(bq *c, float hz, float db, float sr)
{
  float w = hz / sr;
  float sn = vd_sin(w), cs = vd_sin(w + 0.25f);
  float A = vd_exp2(db * 0.0830192f);
  float al = 0.5f * sn;                /* alpha, Q = 1 */
  c->b0 = 1.0f + al * A;
  c->b1 = -2.0f * cs;
  c->b2 = 1.0f - al * A;
  c->a1 = -2.0f * cs;
  c->a2 = 1.0f - al / A;
  bq_norm(c, 1.0f + al / A);
}

static void bq_highshelf(bq *c, float hz, float db, float sr)
{
  float w = hz / sr;
  float sn = vd_sin(w), cs = vd_sin(w + 0.25f);
  float A = vd_exp2(db * 0.0830192f);
  float rt = vd_sqrt(A);
  float al = 1.4142f * rt * sn;        /* 2*sqrt(A)*alpha, shelf S = 1 */
  c->b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + al);
  c->b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
  c->b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - al);
  c->a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
  c->a2 = (A + 1.0f) - (A - 1.0f) * cs - al;
  bq_norm(c, (A + 1.0f) - (A - 1.0f) * cs + al);
}

static inline float bq_tick(const bq *c, float *z, float x)
{
  float y = c->b0 * x + z[0];
  z[0] = c->b1 * x - c->a1 * y + z[1];
  z[1] = c->b2 * x - c->a2 * y;
  return y;
}

static float line_read(const float *buf, int size, int w, float back)
{
  int base = (int)back, r;
  float frac = back - (float)base, a, b;
  r = w - base;
  while (r < 0) r += size;
  while (r >= size) r -= size;
  b = buf[r];
  r--; if (r < 0) r += size;
  a = buf[r];
  return a * frac + b * (1.0f - frac);
}

void fx_init(float sample_rate)
{
  int i;
  fx_sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
  fx_ratio = fx_sr / 44100.0f;
  dl_len = (int)(0.375f * fx_sr + 0.5f);
  if (dl_len < 64) dl_len = 64;
  if (dl_len > DELAY_MAX) dl_len = DELAY_MAX;
  for (i = 0; i < REV_COMBS; i++) {
    rev_cl[i] = (int)(rev_room[i] * fx_ratio + 0.5f);
    if (rev_cl[i] < 32) rev_cl[i] = 32;
    if (rev_cl[i] > REV_C_MAX) rev_cl[i] = REV_C_MAX;
  }
  for (i = 0; i < REV_APS; i++) {
    rev_al[i] = (int)(rev_ap_base[i] * fx_ratio + 0.5f);
    if (rev_al[i] < 16) rev_al[i] = 16;
    if (rev_al[i] > REV_AP_MAX) rev_al[i] = REV_AP_MAX;
  }
  vd_svf_set(&les_split_c, 800.0f, 0.7071f, fx_sr);
  les_horn_hp_coef = vd_coef(1.0f / (VD_TAU * 140.0f), fx_sr);
  les_drum_tone_coef = vd_coef(1.0f / (VD_TAU * 2600.0f), fx_sr);
  les_base_delay = 0.004f * fx_sr;
  les_dist_scale = fx_sr / LES_C;
  les_center_d = vd_sqrt(LES_MIC_X * LES_MIC_X + LES_MIC_Y * LES_MIC_Y);
  fx_reset();
}

void fx_reset(void)
{
  int i, c, a, ch, o;
  sat_dc[0] = sat_dc[1] = 0.0f;
  comp_env = 0.0f; comp_gain = 1.0f;
  for (ch = 0; ch < 2; ch++) {
    for (c = 0; c < 3; c++) eq_z[ch][c][0] = eq_z[ch][c][1] = 0.0f;
    tone_f[ch].ic1 = tone_f[ch].ic2 = 0.0f;
    for (o = 0; o < 4; o++)
      for (i = 0; i < 512; i++) ph_buf[ch][o][i] = 0.0f;
    for (i = 0; i < CHORUS_LEN; i++) ch_buf[ch][i] = 0.0f;
    for (i = 0; i < FLANGER_LEN; i++) fl_buf[ch][i] = 0.0f;
    for (i = 0; i < DELAY_MAX; i++) dl_buf[ch][i] = 0.0f;
    for (c = 0; c < REV_COMBS; c++) {
      rev_cp[ch][c] = 0; rev_clp[ch][c] = 0.0f;
      for (i = 0; i < REV_C_MAX; i++) rev_cb[ch][c][i] = 0.0f;
    }
    for (a = 0; a < REV_APS; a++) {
      rev_ap[ch][a] = 0;
      for (i = 0; i < REV_AP_MAX; i++) rev_ab[ch][a][i] = 0.0f;
    }
  }
  ph_pos[0] = ph_pos[1] = 0; ph_phase = 0.0f;
  ch_pos[0] = ch_pos[1] = 0; ch_phase = 0.0f;
  fl_pos[0] = fl_pos[1] = 0; fl_phase = 0.0f;
  wah_f[0].ic1 = wah_f[0].ic2 = 0.0f;
  wah_f[1].ic1 = wah_f[1].ic2 = 0.0f;
  wah_env = 0.0f; wah_cut = 800.0f;
  ring_phase = 0.0f; ring_inc = 0.0f;
  crush_hold[0] = crush_hold[1] = 0.0f;
  crush_levels = 512.0f; crush_count = 0; crush_div = 1;
  trem_phase = 0.0f; pan_phase = 0.0f;
  dl_pos[0] = dl_pos[1] = 0; dl_lp[0] = dl_lp[1] = 0.0f;
  dl_wow = 0.0f; dl_wow_phase = 0.0f;
  fx_process_fb = 0.0f; fx_process_damp = 0.0f;
  les_split.ic1 = les_split.ic2 = 0.0f;
  les_horn_lp = 0.0f; les_drum_tone = 0.0f;
  les_w = 0;
  les_horn.phase = les_drum.phase = 0.0f;
  les_horn.hz = les_drum.hz = 0.0f;
  les_horn.target_hz = les_drum.target_hz = 0.0f;
  les_horn.coef = les_drum.coef = 0.0f;
  rotary_wet = 0.0f;
  for (i = 0; i < ROTOR_LEN; i++) les_hbuf[i] = les_dbuf[i] = 0.0f;
  rev_phase = 0.0f;
  __atomic_store_n(&rotor_hz100, 0, __ATOMIC_RELEASE);
}

int fx_rotor_hz100(void)
{
  return __atomic_load_n(&rotor_hz100, __ATOMIC_ACQUIRE);
}

/* Motor speed uses two time constants: one for spin-up, one for coast/brake. */
static void les_motor_set(les_rotor *q, float target, float up_s, float down_s,
                          float stop_s, float sr)
{
  float t = target > q->target_hz ? up_s : (target <= 0.0001f ? stop_s : down_s);
  q->target_hz = target;
  q->coef = vd_coef(t, sr);
}

static void les_motor_tick(les_rotor *q, float sr)
{
  q->hz += (q->target_hz - q->hz) * q->coef;
  if (q->target_hz <= 0.0001f && q->hz < 0.002f) q->hz = 0.0f;
  if (q->hz < 0.0f) q->hz = 0.0f;
  q->phase += q->hz / sr;
  if (q->phase >= 1.0f) q->phase -= 1.0f;
}

/* One rotor heard by one microphone. Three baffle taps use a geometry-based
 * path length and inverse-distance/directivity gains. */
static float les_rotor_read(const float *buf, float phase, float radius,
                            float mic_x, float depth)
{
  float s = vd_sin(phase), c = vd_sin(phase + 0.25f);
  float out = 0.0f;
  int k;
  for (k = 0; k < LES_TAPS; k++) {
    float ts = s * les_tap_cos[k] + c * les_tap_sin[k];
    float tc = c * les_tap_cos[k] - s * les_tap_sin[k];
    float dx = mic_x - radius * tc;
    float dy = LES_MIC_Y - radius * ts;
    float d = vd_sqrt(dx * dx + dy * dy);
    if (d < 0.001f) d = 0.001f;
    {
      float path = (d - les_center_d) * les_dist_scale * depth;
      float tap = line_read(buf, ROTOR_LEN, les_w, les_base_delay + path);
      float dot = (c * dx + s * dy) / d;
      float dir = 0.50f + 0.50f * dot;
      if (dir < 0.08f) dir = 0.08f;
      out += tap * les_tap_gain[k] * dir * (les_center_d / d);
    }
  }
  return out;
}

static float saturate(float x, int type, float drive, float *dc)
{
  float g = 1.0f + 5.0f * drive;
  x *= g;
  if (type == 2) {                    /* tube: asymmetric soft clip, DC blocked */
    float y = vd_tanh(x * 0.85f);
    y = vd_tanh(y + 0.16f * y * y);
    *dc += (y - *dc) * 0.0005f;
    return y - *dc;
  }
  return vd_tanh(x);                  /* solid state */
}

static float trem_wave(float ph, int shape)
{
  float s = vd_sin(ph);
  if (shape == 1) return 1.0f - 4.0f * (ph < 0.5f ? ph : 1.0f - ph); /* triangle */
  if (shape == 2) return s >= 0.0f ? 1.0f : -1.0f;                   /* square  */
  return s;
}

void fx_process(float *l, float *r, int n, const int *a)
{
  float f[FX_COUNT];
  int i, ch, c, o;
  float sat_type, drive, comp_on, comp_amt, tone;
  float ph_mix, ph_rate, ph_depth, ch_mix, ch_rate, ch_depth;
  float fl_mix, fl_rate, fl_depth, wah_mix, wah_depth;
  float ring_mix, ring_depth, crush_mix;
  float trem_depth, trem_rate, pan_depth, pan_rate;
  float rot_depth, rot_drive, horn_gain, drum_gain, rotary_wet_target;
  int trem_shape, rotary_on, rotary_speed, delay_type, reverb_type;

  if (!l || !r || !a || n <= 0) return;
  for (i = 0; i < FX_COUNT; i++) f[i] = q1000(a[i]);
  if (f[FX_MASTER] < 0.0f) f[FX_MASTER] = 0.0f;

  sat_type = f[FX_SAT_TYPE];
  drive = clamp01(f[FX_DRIVE]);
  comp_on = f[FX_COMP_ON] > 0.5f ? 1.0f : 0.0f;
  comp_amt = clamp01(f[FX_COMP_AMT]);
  tone = clamp01(f[FX_TONE]);
  ph_mix = clamp01(f[FX_PHASER_MIX]);
  ph_rate = f[FX_PHASER_RATE];
  ph_depth = clamp01(f[FX_PHASER_DEPTH]);
  ch_mix = clamp01(f[FX_CHORUS_MIX]);
  ch_rate = f[FX_CHORUS_RATE];
  ch_depth = clamp01(f[FX_CHORUS_DEPTH]);
  fl_mix = clamp01(f[FX_FLANGER_MIX]);
  fl_rate = f[FX_FLANGER_RATE] > 0.0f ? f[FX_FLANGER_RATE] : 0.35f;
  if (fl_rate < 0.05f) fl_rate = 0.05f;
  if (fl_rate > 8.0f) fl_rate = 8.0f;
  fl_depth = clamp01(f[FX_FLANGER_DEPTH]);
  wah_mix = clamp01(f[FX_WAH_MIX]);
  wah_depth = clamp01(f[FX_WAH_DEPTH]);
  ring_mix = clamp01(f[FX_RING_MIX]);
  ring_depth = clamp01(f[FX_RING_DEPTH]);
  crush_mix = clamp01(f[FX_CRUSH_MIX]);
  trem_depth = clamp01(f[FX_TREM_DEPTH]);
  trem_rate = f[FX_TREM_RATE];
  pan_depth = clamp01(f[FX_PAN_DEPTH]);
  pan_rate = f[FX_PAN_RATE];
  trem_shape = (int)(f[FX_TREM_SHAPE] + 0.5f);
  rotary_on = f[FX_ROTARY_ON] > 0.5f ? 1 : 0;
  rotary_speed = (int)(f[FX_ROTARY_SPEED] + 0.5f);
  rot_depth = 0.12f + 0.88f * clamp01(f[FX_ROTARY_DEPTH]);
  rot_drive = clamp01(f[FX_ROTARY_DRIVE]);
  {
    float bal = clamp01(f[FX_ROTARY_BALANCE]);
    horn_gain = 0.55f + 0.90f * bal;
    drum_gain = 0.55f + 0.90f * (1.0f - bal);
  }
  rotary_wet_target = rotary_on ? 1.0f : 0.0f;
  delay_type = (int)(f[FX_DELAY_TYPE] + 0.5f);
  reverb_type = (int)(f[FX_REVERB_TYPE] + 0.5f);

  /* --- block-rate coefficient setup -------------------------------------- */
  bq_lowshelf(&eq_b[0][0], 180.0f, f[FX_EQ_BASS], fx_sr);
  bq_peak(&eq_b[0][1], 900.0f, f[FX_EQ_MID], fx_sr);
  bq_highshelf(&eq_b[0][2], 3200.0f, f[FX_EQ_TREBLE], fx_sr);
  eq_b[1][0] = eq_b[0][0]; eq_b[1][1] = eq_b[0][1]; eq_b[1][2] = eq_b[0][2];

  {
    float cut = 700.0f * vd_pow(23.0f, tone);
    if (cut > fx_sr * 0.45f) cut = fx_sr * 0.45f;
    vd_svf_set(&tone_c, cut, 0.707f, fx_sr);
  }

  /* Auto-wah cutoff follows the detector from the previous block. Smoothing
   * here keeps the filter sweep free of zipper noise at any block size. */
  {
    float target = 280.0f + 2800.0f *
      clamp01(0.20f + 3.2f * wah_env * (0.15f + 0.85f * wah_depth));
    float k = vd_coef(0.02f, fx_sr) * (float)n;
    if (k > 1.0f) k = 1.0f;
    wah_cut += (target - wah_cut) * k;
    vd_svf_set(&wah_c, wah_cut, 3.2f, fx_sr);
  }

  {
    float hz = f[FX_RING_FREQ] > 0.0f ? f[FX_RING_FREQ] : 110.0f;
    if (hz < 20.0f) hz = 20.0f;
    if (hz > 2000.0f) hz = 2000.0f;
    ring_inc = hz / fx_sr;
  }

  {
    float bits = f[FX_CRUSH_BITS] > 0.0f ? f[FX_CRUSH_BITS] : 10.0f;
    float rate = f[FX_CRUSH_RATE] > 0.0f ? f[FX_CRUSH_RATE] : 16.0f;
    if (bits < 4.0f) bits = 4.0f;
    if (bits > 16.0f) bits = 16.0f;
    if (rate < 1.0f) rate = 1.0f;
    if (rate > 64.0f) rate = 64.0f;
    crush_levels = vd_pow(2.0f, bits - 1.0f);
    crush_div = (int)(rate + 0.5f);
  }

  {
    float ms = f[FX_DELAY_MS];
    int len;
    if (ms < 60.0f) ms = 60.0f;
    if (ms > 1200.0f) ms = 1200.0f;
    len = (int)(ms * 0.001f * fx_sr + 0.5f);
    if (len < 64) len = 64;
    if (len > DELAY_MAX) len = DELAY_MAX;
    dl_len = len;
  }

  if (reverb_type > 0) {
    const int *base = rev_room;
    float fb, damp;
    if (reverb_type == 2) base = rev_hall;
    else if (reverb_type == 3) base = rev_plate;
    else if (reverb_type == 4) base = rev_spring;
    for (c = 0; c < REV_COMBS; c++) {
      int len = (int)(base[c] * fx_ratio + 0.5f);
      if (len < 32) len = 32;
      if (len > REV_C_MAX) len = REV_C_MAX;
      rev_cl[c] = len;
    }
    {
      float dec = clamp01(f[FX_REVERB_DECAY]);
      float dmp = clamp01(f[FX_REVERB_DAMP]);
      if (reverb_type == 1) { fb = 0.60f + 0.28f * dec; damp = 0.40f + 0.50f * dmp; }
      else if (reverb_type == 2) { fb = 0.70f + 0.27f * dec; damp = 0.25f + 0.50f * dmp; }
      else if (reverb_type == 3) { fb = 0.66f + 0.30f * dec; damp = 0.08f + 0.45f * dmp; }
      else { fb = 0.72f + 0.25f * dec; damp = 0.45f + 0.48f * dmp; }
    }
    /* keep these in file-scope scratch used by the sample loop below */
    fx_process_fb = fb;
    fx_process_damp = damp;
  }

  {
    float hz_h = 0.0f, hz_d = 0.0f;
    if (rotary_on && rotary_speed == 2) { hz_h = 7.0f; hz_d = 6.0f; }
    else if (rotary_on && rotary_speed == 1) { hz_h = 0.90f; hz_d = 0.80f; }
    les_motor_set(&les_horn, hz_h, 0.22f, 0.38f, 0.55f, fx_sr);
    les_motor_set(&les_drum, hz_d, 1.10f, 0.65f, 1.40f, fx_sr);
    rotary_wet_coef = vd_coef(0.025f, fx_sr);
  }

  /* --- per-sample chain --------------------------------------------------- */
  for (i = 0; i < n; i++) {
    float xl = l[i], xr = r[i];
    float env, gain;

    /* saturation */
    if (sat_type > 0.5f) {
      xl = saturate(xl, (int)(sat_type + 0.5f), drive, &sat_dc[0]);
      xr = saturate(xr, (int)(sat_type + 0.5f), drive, &sat_dc[1]);
    }

    /* linked compressor */
    if (comp_on) {
      env = vd_maxf(vd_absf(xl), vd_absf(xr));
      comp_env += (env - comp_env) *
        vd_coef(env > comp_env ? 0.008f : 0.16f, fx_sr);
      {
        float thr = 0.45f - 0.22f * comp_amt;
        float over = comp_env > thr ? comp_env - thr : 0.0f;
        float g = 1.0f / (1.0f + over * comp_amt * 6.0f);
        g *= 1.0f + 0.35f * comp_amt;
        gain = g;
      }
      comp_gain += (gain - comp_gain) * vd_coef(0.01f, fx_sr);
      xl *= comp_gain; xr *= comp_gain;
    }

    /* 3-band EQ */
    for (ch = 0; ch < 2; ch++) {
      float y = ch == 0 ? xl : xr;
      for (c = 0; c < 3; c++) y = bq_tick(&eq_b[ch][c], eq_z[ch][c], y);
      if (ch == 0) xl = y; else xr = y;
    }

    /* Envelope-following auto-wah: bandpass sweep, no IRs or borrowed tables. */
    {
      float env = vd_maxf(vd_absf(xl), vd_absf(xr));
      wah_env += (env - wah_env) *
        vd_coef(env > wah_env ? 0.012f : 0.16f, fx_sr);
      if (wah_mix > 0.0005f) {
        vd_svf_out wl = vd_svf_tick(&wah_f[0], &wah_c, xl);
        vd_svf_out wr = vd_svf_tick(&wah_f[1], &wah_c, xr);
        float wet_l = wl.bp * 2.2f, wet_r = wr.bp * 2.2f;
        xl += (wet_l - xl) * wah_mix;
        xr += (wet_r - xr) * wah_mix;
      }
    }

    /* Ring modulation: a sine multiplier with dry/wet and depth control. */
    ring_phase += ring_inc;
    if (ring_phase >= 1.0f) ring_phase -= 1.0f;
    if (ring_mix > 0.0005f) {
      float mod = (1.0f - ring_depth) + ring_depth * vd_sin(ring_phase);
      xl += (xl * mod - xl) * ring_mix;
      xr += (xr * mod - xr) * ring_mix;
    }

    /* 4-stage phaser, shared LFO. Always tick so enabling cannot click. */
    {
      float g = 0.15f + 0.72f * ph_depth *
                (0.5f + 0.5f * vd_sin(ph_phase));
      ph_phase += ph_rate / fx_sr;
      if (ph_phase >= 1.0f) ph_phase -= 1.0f;
      for (ch = 0; ch < 2; ch++) {
        float dry = ch == 0 ? xl : xr;
        float y = dry;
        for (o = 0; o < 4; o++) {
          float yy = ph_buf[ch][o][ph_pos[ch]];
          ph_buf[ch][o][ph_pos[ch]] = y + yy * g;
          y = yy - g * y;
        }
        y = dry * (1.0f - ph_mix) + y * ph_mix;
        if (ch == 0) xl = y; else xr = y;
      }
    }

    /* chorus: two modulated taps per channel, always ticked */
    {
      float p1 = vd_sin(ch_phase), p2 = vd_sin(ch_phase + 0.5f);
      float base1 = 610.0f * fx_ratio, base2 = 480.0f * fx_ratio;
      float dep1 = 110.0f * fx_ratio * ch_depth;
      float dep2 = 90.0f * fx_ratio * ch_depth;
      float t1 = line_read(ch_buf[0], CHORUS_LEN, ch_pos[0], base1 + dep1 * p1);
      float t2 = line_read(ch_buf[1], CHORUS_LEN, ch_pos[1], base2 + dep2 * p2);
      float dry = 1.0f - 0.35f * ch_mix, wet = 0.85f * ch_mix;
      ch_buf[0][ch_pos[0]] = xl;
      ch_buf[1][ch_pos[1]] = xr;
      ch_pos[0]++; if (ch_pos[0] >= CHORUS_LEN) ch_pos[0] = 0;
      ch_pos[1]++; if (ch_pos[1] >= CHORUS_LEN) ch_pos[1] = 0;
      ch_phase += ch_rate / fx_sr;
      if (ch_phase >= 1.0f) ch_phase -= 1.0f;
      xl = xl * dry + t1 * wet;
      xr = xr * dry + t2 * wet;
    }

    /* Flanger: short feedback comb with a slow geometric sweep. */
    {
      float sweep = 0.5f + 0.5f * vd_sin(fl_phase);
      float back = 2.0f * fx_ratio +
                   (1.0f + 8.0f * fl_depth * fx_ratio) * sweep;
      float fb = 0.22f + 0.30f * fl_depth;
      float yl = line_read(fl_buf[0], FLANGER_LEN, fl_pos[0], back);
      float yr = line_read(fl_buf[1], FLANGER_LEN, fl_pos[1], back);
      fl_buf[0][fl_pos[0]] = xl + yl * fb;
      fl_buf[1][fl_pos[1]] = xr + yr * fb;
      fl_pos[0]++; if (fl_pos[0] >= FLANGER_LEN) fl_pos[0] = 0;
      fl_pos[1]++; if (fl_pos[1] >= FLANGER_LEN) fl_pos[1] = 0;
      fl_phase += fl_rate / fx_sr;
      if (fl_phase >= 1.0f) fl_phase -= 1.0f;
      xl += (yl - xl) * fl_mix;
      xr += (yr - xr) * fl_mix;
    }

    /* tremolo and auto-pan */
    {
      float sL = 0.5f + 0.5f * trem_wave(trem_phase, trem_shape);
      float sR = 0.5f + 0.5f * trem_wave(trem_phase + 0.08f, trem_shape);
      float aL = 1.0f - trem_depth * (1.0f - sL);
      float aR = 1.0f - trem_depth * (1.0f - sR);
      float pos = pan_depth * vd_sin(pan_phase);
      float pL = 1.4142f * vd_sqrt(1.0f - pos);
      float pR = 1.4142f * vd_sqrt(1.0f + pos);
      trem_phase += trem_rate / fx_sr;
      if (trem_phase >= 1.0f) trem_phase -= 1.0f;
      pan_phase += pan_rate / fx_sr;
      if (pan_phase >= 1.0f) pan_phase -= 1.0f;
      xl *= aL * pL;
      xr *= aR * pR;
    }

    /* Motors coast even when the wet mix has faded out. */
    les_motor_tick(&les_horn, fx_sr);
    les_motor_tick(&les_drum, fx_sr);

    /* Leslie: clean dual-rotor model with separate horn/drum motors. */
    if (rotary_wet > 0.0005f || rotary_on) {
      float mono = (xl + xr) * 0.5f;
      float dry_l = xl, dry_r = xr;
      vd_svf_out sv;
      float horn, drum, lpn, hl, hr, bl, br, wet_l, wet_r;

      sv = vd_svf_tick(&les_split, &les_split_c, mono);
      lpn = les_horn_lp + (sv.hp - les_horn_lp) * les_horn_hp_coef;
      horn = sv.hp - lpn;
      les_horn_lp = lpn;
      les_drum_tone += (sv.lp - les_drum_tone) * les_drum_tone_coef;
      drum = les_drum_tone;

      if (rot_drive > 0.0f)
        horn = vd_tanh(horn * (1.0f + 5.0f * rot_drive));

      les_hbuf[les_w] = horn;
      les_dbuf[les_w] = drum;
      les_w++; if (les_w >= ROTOR_LEN) les_w = 0;

      hl = les_rotor_read(les_hbuf, les_horn.phase, LES_HORN_R, -LES_MIC_X, rot_depth) * horn_gain;
      hr = les_rotor_read(les_hbuf, les_horn.phase, LES_HORN_R,  LES_MIC_X, rot_depth) * horn_gain;
      bl = les_rotor_read(les_dbuf, les_drum.phase, LES_DRUM_R, -LES_MIC_X, rot_depth) * drum_gain;
      br = les_rotor_read(les_dbuf, les_drum.phase, LES_DRUM_R,  LES_MIC_X, rot_depth) * drum_gain;

      wet_l = (hl + bl) * 0.90f + mono * 0.045f;
      wet_r = (hr + br) * 0.90f + mono * 0.045f;
      rotary_wet += (rotary_wet_target - rotary_wet) * rotary_wet_coef;
      if (rotary_wet > 1.0f) rotary_wet = 1.0f;
      xl = dry_l + (wet_l - dry_l) * rotary_wet;
      xr = dry_r + (wet_r - dry_r) * rotary_wet;
    }

    /* stereo / tape delay */
    if (delay_type > 0 && f[FX_DELAY_MIX] > 0.0f) {
      float mix = clamp01(f[FX_DELAY_MIX]);
      float fb = clamp01(f[FX_DELAY_FB]) * (delay_type == 2 ? 0.82f : 0.85f);
      float yl = line_read(dl_buf[0], dl_len, dl_pos[0], (float)dl_len + dl_wow);
      float yr = line_read(dl_buf[1], dl_len, dl_pos[1], (float)dl_len - dl_wow);
      dl_lp[0] += (yl - dl_lp[0]) * (delay_type == 2 ? 0.28f : 0.55f);
      dl_lp[1] += (yr - dl_lp[1]) * (delay_type == 2 ? 0.28f : 0.55f);
      if (delay_type == 1) {              /* ping-pong cross-feed */
        dl_buf[0][dl_pos[0]] = xl + dl_lp[1] * fb;
        dl_buf[1][dl_pos[1]] = xr + dl_lp[0] * fb;
      } else {                            /* tape echo: flutter + damping */
        dl_wow = 6.0f * fx_ratio * vd_sin(dl_wow_phase);
        dl_wow_phase += 0.7f / fx_sr;
        if (dl_wow_phase >= 1.0f) dl_wow_phase -= 1.0f;
        dl_buf[0][dl_pos[0]] = xl + dl_lp[0] * fb;
        dl_buf[1][dl_pos[1]] = xr + dl_lp[1] * fb;
      }
      dl_pos[0]++; if (dl_pos[0] >= dl_len) dl_pos[0] = 0;
      dl_pos[1]++; if (dl_pos[1] >= dl_len) dl_pos[1] = 0;
      xl += yl * mix;
      xr += yr * mix;
    }

    /* reverb: 6 combs + 2 allpass per channel */
    if (reverb_type > 0 && f[FX_REVERB_MIX] > 0.0f) {
      float mix = clamp01(f[FX_REVERB_MIX]);
      float sl = 0.0f, sr = 0.0f, wl, wr;
      float fb = fx_process_fb, damp = fx_process_damp;
      if (reverb_type == 4) {             /* spring: gentle metallic warble */
        fb += 0.015f * vd_sin(rev_phase);
        rev_phase += 3.1f / fx_sr;
        if (rev_phase >= 1.0f) rev_phase -= 1.0f;
      }
      for (c = 0; c < REV_COMBS; c++) {
        int len = rev_cl[c], p = rev_cp[0][c];
        float y = rev_cb[0][c][p];
        float lp = rev_clp[0][c] + (y - rev_clp[0][c]) * damp;
        rev_clp[0][c] = lp;
        rev_cb[0][c][p] = (xl + xr * 0.18f) + lp * fb;
        if (++p >= len) p = 0;
        rev_cp[0][c] = p;
        sl += y;
        p = rev_cp[1][c];
        y = rev_cb[1][c][p];
        lp = rev_clp[1][c] + (y - rev_clp[1][c]) * damp;
        rev_clp[1][c] = lp;
        rev_cb[1][c][p] = (xr + xl * 0.18f) + lp * fb;
        if (++p >= len) p = 0;
        rev_cp[1][c] = p;
        sr += y;
      }
      wl = sl / (float)REV_COMBS;
      wr = sr / (float)REV_COMBS;
      for (c = 0; c < REV_APS; c++) {
        int len = rev_al[c];
        int p = rev_ap[0][c];
        float in = wl, y = rev_ab[0][c][p];
        wl = y - 0.5f * in;
        rev_ab[0][c][p] = in + 0.5f * wl;
        if (++p >= len) p = 0;
        rev_ap[0][c] = p;
        p = rev_ap[1][c];
        in = wr; y = rev_ab[1][c][p];
        wr = y - 0.5f * in;
        rev_ab[1][c][p] = in + 0.5f * wr;
        if (++p >= len) p = 0;
        rev_ap[1][c] = p;
      }
      xl = xl * (1.0f - 0.40f * mix) + wl * (1.6f * mix);
      xr = xr * (1.0f - 0.40f * mix) + wr * (1.6f * mix);
    }

    /* Bitcrusher: sample-and-hold decimation plus reduced word length. The
     * hold stage always runs so switching the effect on cannot click. */
    if (--crush_count <= 0) {
      float ql = vd_clamp(xl, -2.0f, 2.0f) * crush_levels;
      float qr = vd_clamp(xr, -2.0f, 2.0f) * crush_levels;
      crush_hold[0] = (float)(int)ql / crush_levels;
      crush_hold[1] = (float)(int)qr / crush_levels;
      crush_count = crush_div;
    }
    if (crush_mix > 0.0005f) {
      xl += (crush_hold[0] - xl) * crush_mix;
      xr += (crush_hold[1] - xr) * crush_mix;
    }

    /* master tone, limiter and level */
    xl = vd_svf_tick(&tone_f[0], &tone_c, xl).lp;
    xr = vd_svf_tick(&tone_f[1], &tone_c, xr).lp;
    xl = vd_tanh(xl * 1.15f) * f[FX_MASTER];
    xr = vd_tanh(xr * 1.15f) * f[FX_MASTER];
    if (xl > 0.99f) xl = 0.99f; else if (xl < -0.99f) xl = -0.99f;
    if (xr > 0.99f) xr = 0.99f; else if (xr < -0.99f) xr = -0.99f;
    if (!(xl > -1.0e20f && xl < 1.0e20f)) xl = 0.0f;
    if (!(xr > -1.0e20f && xr < 1.0e20f)) xr = 0.0f;
    l[i] = xl;
    r[i] = xr;
  }
  __atomic_store_n(&rotor_hz100, (int)(les_horn.hz * 100.0f + 0.5f), __ATOMIC_RELEASE);
}
