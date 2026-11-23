/* BAR-FLY -- a lounge electric piano for VIBE FM.
 *
 * Control flow: parameters, presets, keys/pads, display, editor.
 * Audio flow:   real Nuked OPL3 voices + synthesized bossa drums -> EP FX -> output.
 *
 * Sound is immediate: keys and pads play the BAR FLY voice from startup.
 * Pads 1-8 play four-note bossa voicings; pads 9-16 launch one of eight
 * 16-step bossa grooves. The six keys right of ENTER are
 * SOUND / ENV / LFO / FX / SPACE / DEMO.
 */
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"
#include "fm.h"
#include "drums.h"
#include "fx.h"

VIBE_PARALLEL_AUDIO

/* ------------------------------------------------------------------ parameters */
enum {
  P_PRESET = 0,
  P_VOLUME, P_OCTAVE, P_TRANSPOSE, P_VELOCITY, P_ATTACK, P_RELEASE, P_SPREAD,
  P_FM_INDEX, P_FM_DECAY, P_FM_FEEDBACK, P_FM_HARMONIC, P_FM_DETUNE, P_SUB,
  P_BRIGHT,
  P_SAT_TYPE, P_DRIVE, P_COMP_ON, P_COMP_AMT,
  P_EQ_BASS, P_EQ_MID, P_EQ_TREBLE,
  P_PHASER_MIX, P_PHASER_RATE, P_PHASER_DEPTH,
  P_CHORUS_MIX, P_CHORUS_RATE, P_CHORUS_DEPTH,
  P_TREM_DEPTH, P_TREM_RATE, P_TREM_SHAPE,
  P_PAN_DEPTH, P_PAN_RATE,
  P_ROTARY_ON, P_ROTARY_SPEED, P_ROTARY_DEPTH, P_ROTARY_DRIVE,
  P_ROTARY_BALANCE,
  P_DELAY_TYPE, P_DELAY_MIX, P_DELAY_MS, P_DELAY_FB,
  P_REVERB_TYPE, P_REVERB_MIX, P_REVERB_DECAY, P_REVERB_DAMP,
  P_FLANGER_MIX, P_FLANGER_RATE, P_FLANGER_DEPTH,
  P_WAH_MIX, P_WAH_DEPTH,
  P_RING_MIX, P_RING_FREQ, P_RING_DEPTH,
  P_CRUSH_MIX, P_CRUSH_BITS, P_CRUSH_RATE,
  P_VIB_DEPTH, P_VIB_RATE,
  P_MASTER, P_DEMO_RIFF, P_DEMO_BPM,
  /* Bossa drum machine. Ids are appended: existing ids never change. */
  P_GROOVE_RUN, P_GROOVE_PATTERN, P_GROOVE_SONG,
  P_GROOVE_BPM, P_GROOVE_SWING, P_GROOVE_HUMANIZE,
  P_GROOVE_LEVEL, P_GROOVE_TUNE, P_GROOVE_DECAY,
  P_COUNT
};
#define P_COLOR_FIRST P_FLANGER_MIX
#define P_COLOR_LAST  P_CRUSH_RATE

static const char *const PRESET_NAMES[] = {
  "MK I RHODES", "MK II RHODES", "WURLI 200A", "CP-70", "OPL3 E.PIANO",
  "NORD STAGE", "SOUL / MOTOWN", "NEO-SOUL", "JAZZ ROOM", "HAMMOND KEYS",
  "OPL3 BELL", "STAGE PIANO", "ELECTRIC ORGAN", "VOX CONTINENTAL",
  "FARFISA COMPACT", "ACE TONE", "CHEEZE ORG", "60S ORGAN",
  "PERCUSSIVE ORGAN", "FM5 GRAND PIANO"
};
#define NPRESET (int)(sizeof(PRESET_NAMES) / sizeof(PRESET_NAMES[0]))

static const char *const ON_OFF[]      = { "OFF", "ON" };
static const char *const SAT_TYPES[]   = { "OFF", "SOLID", "TUBE" };
static const char *const SPEEDS[]      = { "STOP", "SLOW", "FAST" };
static const char *const DELAY_TYPES[] = { "OFF", "PING-PONG", "TAPE" };
static const char *const REV_TYPES[]   = { "OFF", "ROOM", "HALL", "PLATE", "SPRING" };
static const char *const WAVES[]       = { "SINE", "TRI", "SQUARE" };
static const char *const GROOVE_NAMES[BF_DRUM_PATTERNS] = {
  "BASIC BOSSA", "ANTICIPATED", "LOUNGE BAR", "SYNCOPATED",
  "JAZZ CLUB", "HOTEL CASIO", "SAMBA", "LOOSE ENDING"
};

#define PV(i,n,min,max,def,u) { i,n,min,max,def,u,0,0 }
#define PE(i,n,min,max,def,u,st,ch) { i,n,min,max,def,u,st,ch }
static const vibe_param PARAMS[P_COUNT] = {
  PE("preset", "Preset", 0, (float)(NPRESET - 1), 0, "", NPRESET, PRESET_NAMES),
  PV("volume", "Volume", 0.0f, 1.5f, 0.9f, ""),
  PV("octave", "Octave", -2.0f, 2.0f, 0.0f, ""),
  PV("transpose", "Transpose", -24.0f, 24.0f, 0.0f, "st"),
  PV("velocity", "Velocity Sens", 0.0f, 1.0f, 0.5f, ""),
  PV("attack", "Attack", 0.1f, 3.0f, 1.0f, "x"),
  PV("release", "Release", 0.1f, 3.0f, 1.0f, "x"),
  PV("spread", "Spread", 0.0f, 1.0f, 0.6f, ""),

  PV("fm_index", "Mod Index", 0.0f, 2.0f, 1.0f, "x"),
  PV("fm_decay", "Mod Decay", 0.1f, 2.5f, 1.0f, "x"),
  PV("fm_feedback", "Feedback", 0.0f, 1.0f, 0.0f, ""),
  PV("fm_harmonic", "Harmonics", 0.0f, 2.0f, 1.0f, "x"),
  PV("fm_detune", "Detune", -50.0f, 50.0f, 0.0f, "ct"),
  PV("sub", "Sub Level", 0.0f, 1.0f, 0.12f, ""),
  PV("bright", "Brightness", 0.0f, 1.0f, 0.82f, ""),

  PE("sat_type", "Saturation", 0, 2, 0, "", 3, SAT_TYPES),
  PV("drive", "Drive", 0.0f, 1.0f, 0.0f, ""),
  PE("comp_on", "Compressor", 0, 1, 0, "", 2, ON_OFF),
  PV("comp_amt", "Comp Amount", 0.0f, 1.0f, 0.35f, ""),

  PV("eq_bass", "Bass", -12.0f, 12.0f, 0.0f, "dB"),
  PV("eq_mid", "Middle", -12.0f, 12.0f, 0.0f, "dB"),
  PV("eq_treble", "Treble", -12.0f, 12.0f, 0.0f, "dB"),

  PV("phaser_mix", "Phaser Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("phaser_rate", "Phaser Rate", 0.1f, 8.0f, 0.6f, "Hz"),
  PV("phaser_depth", "Phaser Depth", 0.0f, 1.0f, 0.7f, ""),

  PV("chorus_mix", "Chorus Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("chorus_rate", "Chorus Rate", 0.1f, 5.0f, 0.45f, "Hz"),
  PV("chorus_depth", "Chorus Depth", 0.0f, 1.0f, 0.55f, ""),

  PV("trem_depth", "Tremolo", 0.0f, 1.0f, 0.0f, ""),
  PV("trem_rate", "Trem Rate", 0.1f, 12.0f, 5.2f, "Hz"),
  PE("trem_shape", "Trem Shape", 0, 2, 0, "", 3, WAVES),

  PV("pan_depth", "Auto Pan", 0.0f, 1.0f, 0.0f, ""),
  PV("pan_rate", "Pan Rate", 0.1f, 8.0f, 0.7f, "Hz"),

  PE("rotary_on", "Leslie", 0, 1, 0, "", 2, ON_OFF),
  PE("rotary_speed", "Rotor Speed", 0, 2, 1, "", 3, SPEEDS),
  PV("rotary_depth", "Rotor Depth", 0.0f, 1.0f, 0.8f, ""),
  PV("rotary_drive", "Horn Drive", 0.0f, 1.0f, 0.2f, ""),
  PV("rotary_balance", "Horn / Bass", 0.0f, 1.0f, 0.55f, ""),

  PE("delay_type", "Delay Type", 0, 2, 0, "", 3, DELAY_TYPES),
  PV("delay_mix", "Delay Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("delay_ms", "Delay Time", 60.0f, 1200.0f, 375.0f, "ms"),
  PV("delay_fb", "Delay Feedback", 0.0f, 1.0f, 0.35f, ""),

  PE("reverb_type", "Reverb", 0, 4, 2, "", 5, REV_TYPES),
  PV("reverb_mix", "Reverb Mix", 0.0f, 1.0f, 0.30f, ""),
  PV("reverb_decay", "Decay", 0.0f, 1.0f, 0.55f, ""),
  PV("reverb_damp", "Damping", 0.0f, 1.0f, 0.45f, ""),

  PV("flanger_mix", "Flanger Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("flanger_rate", "Flanger Rate", 0.05f, 8.0f, 0.35f, "Hz"),
  PV("flanger_depth", "Flanger Depth", 0.0f, 1.0f, 0.55f, ""),

  PV("wah_mix", "Auto Wah Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("wah_depth", "Wah Sweep", 0.0f, 1.0f, 0.55f, ""),

  PV("ring_mix", "Ring Mod Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("ring_freq", "Ring Frequency", 20.0f, 2000.0f, 110.0f, "Hz"),
  PV("ring_depth", "Ring Depth", 0.0f, 1.0f, 0.50f, ""),

  PV("crush_mix", "Bitcrush Mix", 0.0f, 1.0f, 0.0f, ""),
  PV("crush_bits", "Crush Bits", 4.0f, 16.0f, 10.0f, ""),
  PV("crush_rate", "Crush Rate", 1.0f, 64.0f, 16.0f, ""),

  PV("vibrato_depth", "Vibrato", 0.0f, 1.0f, 0.0f, ""),
  PV("vibrato_rate", "Vib Rate", 0.1f, 12.0f, 5.0f, "Hz"),

  PV("master", "Master", 0.0f, 1.2f, 0.85f, ""),
  PE("demo_riff", "Bossa Snippet", 0, 1, 0, "", 2, ON_OFF),
  PV("demo_bpm", "Demo Tempo", 60.0f, 180.0f, 112.0f, "bpm"),

  PE("groove_run", "Groove", 0, 1, 0, "", 2, ON_OFF),
  PE("groove_pattern", "Pattern", 0, BF_DRUM_PATTERNS - 1, 0, "",
     BF_DRUM_PATTERNS, GROOVE_NAMES),
  PE("groove_song", "8-Bar Song", 0, 1, 0, "", 2, ON_OFF),
  PV("groove_bpm", "Groove Tempo", 60.0f, 180.0f, 112.0f, "bpm"),
  PV("groove_swing", "Swing", 0.0f, 0.7f, 0.56f, ""),
  PV("groove_humanize", "Humanize", 0.0f, 1.0f, 0.65f, ""),
  PV("groove_level", "Drum Level", 0.0f, 1.5f, 0.85f, ""),
  PV("groove_tune", "Drum Tune", 0.5f, 1.8f, 1.0f, ""),
  PV("groove_decay", "Drum Decay", 0.25f, 2.5f, 1.0f, "")
};
#undef PV
#undef PE

/* Standard-editor mapping: the FX tab collects the whole rack, including the
 * colour effects; delay and reverb stay together under SPACE. Parameter ids
 * are unchanged - only the cards they appear on move. */
static const char *const GROUPS[P_COUNT] = {
  "SOUND · VOICE","SOUND · VOICE","SOUND · VOICE","SOUND · VOICE","SOUND · VOICE","SOUND · VOICE","SOUND · VOICE","SOUND · VOICE",
  "FM · OPERATORS","FM · OPERATORS","FM · OPERATORS","FM · OPERATORS","FM · OPERATORS","FM · OPERATORS",
  "FX · DYNAMICS","FX · DYNAMICS","FX · DYNAMICS","FX · DYNAMICS",
  "TONE · EQ",
  "TONE · EQ","TONE · EQ","TONE · EQ",
  "FX · PHASER","FX · PHASER","FX · PHASER",
  "FX · CHORUS","FX · CHORUS","FX · CHORUS",
  "FX · TREMOLO","FX · TREMOLO","FX · TREMOLO",
  "FX · PAN","FX · PAN",
  "FX · ROTARY","FX · ROTARY","FX · ROTARY","FX · ROTARY","FX · ROTARY",
  "SPACE · DELAY","SPACE · DELAY","SPACE · DELAY","SPACE · DELAY",
  "SPACE · REVERB","SPACE · REVERB","SPACE · REVERB","SPACE · REVERB",
  "FX · FLANGER","FX · FLANGER","FX · FLANGER",
  "FX · AUTO WAH","FX · AUTO WAH",
  "FX · RING MOD","FX · RING MOD","FX · RING MOD",
  "FX · BITCRUSH","FX · BITCRUSH","FX · BITCRUSH",
  "LFO · VIBRATO","LFO · VIBRATO",
  "SYSTEM · OUTPUT",
  "SYSTEM · DEMO","SYSTEM · DEMO",
  "GROOVE · PATTERN","GROOVE · PATTERN","GROOVE · PATTERN",
  "GROOVE · PATTERN","GROOVE · PATTERN","GROOVE · PATTERN",
  "GROOVE · DRUMS","GROOVE · DRUMS","GROOVE · DRUMS"
};

/* Rack order for the standard editor, so the cards read like the signal flow. */
static const vibe_card EDITOR_CARDS[] = {
  { "VOICE",      6, "Preset, level, octave and the four voice values." },
  { "OPERATORS",  6, "Nuked OPL3 FM recipes and drawbar colour." },
  { "EQ",         6, "Brightness and the 3-band electric-piano EQ." },
  { "DYNAMICS",   4, "Tube / solid saturation and the compressor." },
  { "PHASER",     4, "Four-stage phase sweep." },
  { "CHORUS",     4, "Two slowly modulated stereo delay taps." },
  { "TREMOLO",    4, "Amplitude LFO - the Rhodes heartbeat." },
  { "PAN",        4, "Stereo auto-pan." },
  { "ROTARY",     6, "Dual-rotor Leslie: motors, Doppler and mic mix." },
  { "DELAY",      6, "Ping-pong and tape echo." },
  { "REVERB",     6, "Room, hall, plate and spring." },
  { "FLANGER",    4, "Short modulated delay with feedback." },
  { "AUTO WAH",   4, "Envelope-following bandpass sweep." },
  { "RING MOD",   4, "Sine multiplier sidebands." },
  { "BITCRUSH",   4, "Sample-and-hold decimation and bit reduction." },
  { "VIBRATO",    4, "Pitch LFO." },
  { "OUTPUT",     4, "Master level." },
  { "DEMO",       6, "Bossa snippet on / off and its tempo." },
  { "PATTERN",    6, "Eight 16-step bossa grooves: launch, tempo, swing and feel." },
  { "DRUMS",      6, "Synthesized kick, cross-stick and hat: level and character." }
};

/* --- factory presets: sparse overrides, applied on top of the safe defaults - */
enum {
  PI_INDEX, PI_DECAY, PI_HARMONIC, PI_DETUNE, PI_SUB, PI_BRIGHT,
  PI_SAT, PI_DRIVE, PI_COMP_ON, PI_COMP, PI_EQB, PI_EQM, PI_EQT,
  PI_PH_MIX, PI_PH_RATE, PI_PH_DEPTH,
  PI_CH_MIX, PI_CH_RATE, PI_CH_DEPTH,
  PI_TR_DEPTH, PI_TR_RATE, PI_TR_SHAPE, PI_PAN_DEPTH, PI_PAN_RATE,
  PI_ROT_ON, PI_ROT_SPEED, PI_ROT_DEPTH, PI_ROT_DRIVE, PI_ROT_BALANCE,
  PI_DLY_TYPE, PI_DLY_MIX, PI_DLY_MS, PI_DLY_FB,
  PI_RV_TYPE, PI_RV_MIX, PI_RV_DECAY, PI_RV_DAMP,
  PI_VIB_DEPTH, PI_VIB_RATE, PI_N
};
static const int PRESET_IDS[PI_N] = {
  P_FM_INDEX, P_FM_DECAY, P_FM_HARMONIC, P_FM_DETUNE, P_SUB, P_BRIGHT,
  P_SAT_TYPE, P_DRIVE, P_COMP_ON, P_COMP_AMT,
  P_EQ_BASS, P_EQ_MID, P_EQ_TREBLE,
  P_PHASER_MIX, P_PHASER_RATE, P_PHASER_DEPTH,
  P_CHORUS_MIX, P_CHORUS_RATE, P_CHORUS_DEPTH,
  P_TREM_DEPTH, P_TREM_RATE, P_TREM_SHAPE, P_PAN_DEPTH, P_PAN_RATE,
  P_ROTARY_ON, P_ROTARY_SPEED, P_ROTARY_DEPTH, P_ROTARY_DRIVE, P_ROTARY_BALANCE,
  P_DELAY_TYPE, P_DELAY_MIX, P_DELAY_MS, P_DELAY_FB,
  P_REVERB_TYPE, P_REVERB_MIX, P_REVERB_DECAY, P_REVERB_DAMP,
  P_VIB_DEPTH, P_VIB_RATE
};
/* A missing entry here silently shifts every later preset value onto the
 * wrong parameter (and used to turn reverb decay into vibrato depth). */
typedef char preset_ids_must_match[(sizeof(PRESET_IDS)/sizeof(PRESET_IDS[0]) == PI_N) ? 1 : -1];
typedef struct { const char *name; int patch; const float v[PI_N]; } preset_def;
static const preset_def PRESETS[NPRESET] = {
  { "MK I RHODES", 1, {
    [PI_INDEX]=0.95f,[PI_DECAY]=1.05f,[PI_HARMONIC]=0.75f,[PI_DETUNE]=0.0f,
    [PI_SUB]=0.10f,[PI_BRIGHT]=0.68f,[PI_SAT]=2,[PI_DRIVE]=0.10f,
    [PI_COMP_ON]=1,[PI_COMP]=0.25f,[PI_EQB]=2.0f,[PI_EQM]=-2.0f,[PI_EQT]=1.0f,
    [PI_CH_MIX]=0.34f,[PI_CH_RATE]=0.35f,[PI_CH_DEPTH]=0.50f,
    [PI_TR_DEPTH]=0.55f,[PI_TR_RATE]=5.6f,[PI_PAN_DEPTH]=0.15f,[PI_PAN_RATE]=0.5f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.20f,[PI_RV_DECAY]=0.45f,[PI_RV_DAMP]=0.55f }},

  { "MK II RHODES", 2, {
    [PI_INDEX]=1.10f,[PI_HARMONIC]=1.15f,[PI_BRIGHT]=0.88f,
    [PI_SAT]=2,[PI_DRIVE]=0.22f,[PI_COMP_ON]=1,[PI_COMP]=0.30f,
    [PI_EQB]=1.0f,[PI_EQM]=0.0f,[PI_EQT]=4.0f,
    [PI_CH_MIX]=0.42f,[PI_CH_RATE]=0.55f,[PI_CH_DEPTH]=0.65f,
    [PI_TR_DEPTH]=0.35f,[PI_TR_RATE]=6.0f,
    [PI_DLY_TYPE]=1,[PI_DLY_MIX]=0.12f,[PI_DLY_MS]=280.0f,[PI_DLY_FB]=0.25f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.24f,[PI_RV_DECAY]=0.55f,[PI_RV_DAMP]=0.35f }},

  { "WURLI 200A", 3, {
    [PI_INDEX]=0.85f,[PI_DECAY]=0.90f,[PI_HARMONIC]=0.50f,[PI_SUB]=0.05f,
    [PI_BRIGHT]=0.58f,[PI_SAT]=2,[PI_DRIVE]=0.28f,
    [PI_COMP_ON]=1,[PI_COMP]=0.20f,[PI_EQB]=-1.0f,[PI_EQM]=2.5f,[PI_EQT]=1.0f,
    [PI_TR_DEPTH]=0.62f,[PI_TR_RATE]=6.8f,[PI_TR_SHAPE]=0,
    [PI_RV_TYPE]=4,[PI_RV_MIX]=0.22f,[PI_RV_DECAY]=0.35f,[PI_RV_DAMP]=0.55f,
    [PI_VIB_DEPTH]=0.10f,[PI_VIB_RATE]=5.5f }},

  { "CP-70", 4, {
    [PI_INDEX]=0.90f,[PI_HARMONIC]=1.10f,[PI_BRIGHT]=0.95f,
    [PI_SAT]=1,[PI_DRIVE]=0.08f,[PI_COMP_ON]=1,[PI_COMP]=0.28f,
    [PI_EQB]=-1.0f,[PI_EQM]=0.0f,[PI_EQT]=4.5f,
    [PI_CH_MIX]=0.38f,[PI_CH_RATE]=0.7f,[PI_CH_DEPTH]=0.60f,
    [PI_DLY_TYPE]=1,[PI_DLY_MIX]=0.18f,[PI_DLY_MS]=240.0f,[PI_DLY_FB]=0.22f,
    [PI_RV_TYPE]=3,[PI_RV_MIX]=0.22f,[PI_RV_DECAY]=0.40f,[PI_RV_DAMP]=0.30f }},

  { "OPL3 E.PIANO", 21, {
    [PI_INDEX]=1.20f,[PI_DECAY]=0.85f,[PI_HARMONIC]=1.35f,[PI_DETUNE]=8.0f,
    [PI_BRIGHT]=0.92f,[PI_SAT]=1,[PI_DRIVE]=0.06f,[PI_COMP_ON]=0,
    [PI_EQB]=0.0f,[PI_EQM]=-1.0f,[PI_EQT]=5.0f,
    [PI_CH_MIX]=0.26f,[PI_CH_RATE]=1.4f,[PI_CH_DEPTH]=0.35f,
    [PI_DLY_TYPE]=2,[PI_DLY_MIX]=0.16f,[PI_DLY_MS]=320.0f,[PI_DLY_FB]=0.28f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.26f,[PI_RV_DECAY]=0.62f,[PI_RV_DAMP]=0.25f }},

  { "NORD STAGE", 6, {
    [PI_INDEX]=1.00f,[PI_HARMONIC]=1.05f,[PI_BRIGHT]=0.98f,
    [PI_SAT]=0,[PI_COMP_ON]=1,[PI_COMP]=0.18f,
    [PI_EQB]=0.0f,[PI_EQM]=0.0f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.36f,[PI_CH_RATE]=0.5f,[PI_CH_DEPTH]=0.55f,
    [PI_DLY_TYPE]=1,[PI_DLY_MIX]=0.12f,[PI_DLY_MS]=400.0f,[PI_DLY_FB]=0.20f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.24f,[PI_RV_DECAY]=0.50f,[PI_RV_DAMP]=0.35f }},

  { "SOUL / MOTOWN", 7, {
    [PI_INDEX]=0.95f,[PI_HARMONIC]=0.70f,[PI_BRIGHT]=0.72f,
    [PI_SAT]=2,[PI_DRIVE]=0.20f,[PI_COMP_ON]=1,[PI_COMP]=0.42f,
    [PI_EQB]=2.0f,[PI_EQM]=-1.0f,[PI_EQT]=1.0f,
    [PI_PH_MIX]=0.34f,[PI_PH_RATE]=0.5f,[PI_PH_DEPTH]=0.65f,
    [PI_TR_DEPTH]=0.60f,[PI_TR_RATE]=5.8f,
    [PI_RV_TYPE]=3,[PI_RV_MIX]=0.30f,[PI_RV_DECAY]=0.48f,[PI_RV_DAMP]=0.40f }},

  { "NEO-SOUL", 8, {
    [PI_INDEX]=1.00f,[PI_HARMONIC]=0.90f,[PI_BRIGHT]=0.78f,
    [PI_SAT]=2,[PI_DRIVE]=0.38f,[PI_COMP_ON]=1,[PI_COMP]=0.45f,
    [PI_EQB]=2.5f,[PI_EQM]=-2.0f,[PI_EQT]=2.5f,
    [PI_CH_MIX]=0.45f,[PI_CH_RATE]=0.4f,[PI_CH_DEPTH]=0.70f,
    [PI_TR_DEPTH]=0.25f,[PI_TR_RATE]=4.6f,
    [PI_DLY_TYPE]=2,[PI_DLY_MIX]=0.22f,[PI_DLY_MS]=420.0f,[PI_DLY_FB]=0.32f,
    [PI_RV_TYPE]=3,[PI_RV_MIX]=0.32f,[PI_RV_DECAY]=0.62f,[PI_RV_DAMP]=0.35f }},

  { "JAZZ ROOM", 14, {
    [PI_INDEX]=0.35f,[PI_DECAY]=1.20f,[PI_HARMONIC]=0.45f,[PI_BRIGHT]=0.72f,
    [PI_SAT]=0,[PI_DRIVE]=0.0f,[PI_COMP_ON]=1,[PI_COMP]=0.18f,
    [PI_EQB]=1.0f,[PI_EQM]=-1.5f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.28f,[PI_CH_RATE]=0.30f,[PI_CH_DEPTH]=0.30f,
    [PI_TR_DEPTH]=0.28f,[PI_TR_RATE]=5.2f,[PI_TR_SHAPE]=0,
    [PI_RV_TYPE]=1,[PI_RV_MIX]=0.26f,[PI_RV_DECAY]=0.40f,[PI_RV_DAMP]=0.60f }},

  { "HAMMOND KEYS", 9, {
    [PI_INDEX]=0.90f,[PI_HARMONIC]=1.00f,[PI_BRIGHT]=0.90f,[PI_SUB]=0.0f,
    [PI_SAT]=2,[PI_DRIVE]=0.22f,[PI_COMP_ON]=1,[PI_COMP]=0.22f,
    [PI_EQB]=1.0f,[PI_EQM]=-1.0f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.18f,[PI_CH_RATE]=0.6f,[PI_CH_DEPTH]=0.40f,
    [PI_TR_DEPTH]=0.10f,[PI_TR_RATE]=5.0f,
    /* The rotor Doppler reads as pitch wobble on these pure drawbars, so the
     * Leslie is off by default here. F6 demonstrates it on demand. */
    [PI_ROT_ON]=0,[PI_ROT_SPEED]=0,[PI_ROT_DEPTH]=0.0f,[PI_ROT_DRIVE]=0.0f,
    [PI_ROT_BALANCE]=0.55f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.16f,[PI_RV_DECAY]=0.45f,[PI_RV_DAMP]=0.40f }},

  { "OPL3 BELL", 12, {
    [PI_INDEX]=1.00f,[PI_DECAY]=1.00f,[PI_HARMONIC]=1.00f,[PI_DETUNE]=0.0f,
    [PI_SUB]=0.0f,[PI_BRIGHT]=0.95f,[PI_SAT]=0,[PI_DRIVE]=0.0f,
    [PI_COMP_ON]=0,[PI_COMP]=0.0f,[PI_EQB]=0.0f,[PI_EQM]=0.0f,[PI_EQT]=0.0f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.28f,[PI_RV_DECAY]=0.60f,[PI_RV_DAMP]=0.30f,
    [PI_VIB_DEPTH]=0.0f,[PI_VIB_RATE]=5.0f }},

  { "STAGE PIANO", 13, {
    [PI_INDEX]=0.85f,[PI_DECAY]=1.00f,[PI_HARMONIC]=0.80f,[PI_DETUNE]=0.0f,
    [PI_SUB]=0.0f,[PI_BRIGHT]=0.90f,[PI_SAT]=1,[PI_DRIVE]=0.04f,
    [PI_COMP_ON]=1,[PI_COMP]=0.18f,[PI_EQB]=1.0f,[PI_EQM]=-1.0f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.18f,[PI_CH_RATE]=0.35f,[PI_CH_DEPTH]=0.40f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.22f,[PI_RV_DECAY]=0.55f,[PI_RV_DAMP]=0.40f }},

  { "ELECTRIC ORGAN", 15, {
    [PI_INDEX]=0.90f,[PI_HARMONIC]=1.00f,[PI_BRIGHT]=0.92f,[PI_SUB]=0.0f,
    [PI_SAT]=1,[PI_DRIVE]=0.06f,[PI_COMP_ON]=1,[PI_COMP]=0.16f,
    [PI_EQB]=1.0f,[PI_EQM]=-1.0f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.24f,[PI_CH_RATE]=1.1f,[PI_CH_DEPTH]=0.30f,
    [PI_TR_DEPTH]=0.18f,[PI_TR_RATE]=5.5f,[PI_TR_SHAPE]=0,
    [PI_RV_TYPE]=1,[PI_RV_MIX]=0.16f,[PI_RV_DECAY]=0.35f,[PI_RV_DAMP]=0.50f }},

  { "VOX CONTINENTAL", 16, {
    [PI_INDEX]=0.95f,[PI_BRIGHT]=1.00f,[PI_SUB]=0.0f,
    [PI_SAT]=0,[PI_DRIVE]=0.0f,[PI_COMP_ON]=0,
    [PI_EQB]=0.0f,[PI_EQM]=1.5f,[PI_EQT]=3.0f,
    [PI_CH_MIX]=0.20f,[PI_CH_RATE]=1.3f,[PI_CH_DEPTH]=0.35f,
    [PI_TR_DEPTH]=0.14f,[PI_TR_RATE]=6.2f,[PI_TR_SHAPE]=0,
    [PI_RV_TYPE]=1,[PI_RV_MIX]=0.14f,[PI_RV_DECAY]=0.28f,[PI_RV_DAMP]=0.55f }},

  { "FARFISA COMPACT", 17, {
    [PI_INDEX]=1.05f,[PI_BRIGHT]=0.86f,[PI_SUB]=0.0f,
    [PI_SAT]=2,[PI_DRIVE]=0.12f,[PI_COMP_ON]=1,[PI_COMP]=0.20f,
    [PI_EQB]=1.5f,[PI_EQM]=-1.0f,[PI_EQT]=1.0f,
    [PI_TR_DEPTH]=0.28f,[PI_TR_RATE]=6.6f,[PI_TR_SHAPE]=0,
    [PI_CH_MIX]=0.16f,[PI_CH_RATE]=0.9f,[PI_CH_DEPTH]=0.30f,
    [PI_RV_TYPE]=1,[PI_RV_MIX]=0.13f,[PI_RV_DECAY]=0.25f,[PI_RV_DAMP]=0.60f }},

  { "ACE TONE", 18, {
    [PI_INDEX]=0.90f,[PI_BRIGHT]=0.88f,[PI_SUB]=0.0f,
    [PI_SAT]=1,[PI_DRIVE]=0.08f,[PI_COMP_ON]=0,
    [PI_EQB]=1.0f,[PI_EQM]=0.0f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.30f,[PI_CH_RATE]=0.5f,[PI_CH_DEPTH]=0.50f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.18f,[PI_RV_DECAY]=0.40f,[PI_RV_DAMP]=0.45f }},

  { "CHEEZE ORG", 19, {
    [PI_INDEX]=1.00f,[PI_BRIGHT]=0.95f,[PI_SUB]=0.0f,
    [PI_SAT]=1,[PI_DRIVE]=0.10f,[PI_COMP_ON]=0,
    [PI_EQB]=0.5f,[PI_EQM]=-0.5f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.24f,[PI_CH_RATE]=0.6f,[PI_CH_DEPTH]=0.35f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.16f,[PI_RV_DECAY]=0.35f,[PI_RV_DAMP]=0.50f }},

  { "60S ORGAN", 20, {
    [PI_INDEX]=1.00f,[PI_BRIGHT]=0.78f,[PI_SUB]=0.0f,
    [PI_SAT]=0,[PI_DRIVE]=0.0f,[PI_COMP_ON]=0,
    [PI_EQB]=0.5f,[PI_EQM]=-1.0f,[PI_EQT]=0.0f,
    [PI_CH_MIX]=0.18f,[PI_CH_RATE]=0.8f,[PI_CH_DEPTH]=0.30f,
    [PI_TR_DEPTH]=0.10f,[PI_TR_RATE]=5.0f,[PI_TR_SHAPE]=0,
    [PI_RV_TYPE]=1,[PI_RV_MIX]=0.14f,[PI_RV_DECAY]=0.30f,[PI_RV_DAMP]=0.50f }},

  { "PERCUSSIVE ORGAN", 22, {
    [PI_INDEX]=1.00f,[PI_DECAY]=1.00f,[PI_HARMONIC]=1.00f,[PI_DETUNE]=0.0f,
    [PI_SUB]=0.0f,[PI_BRIGHT]=0.90f,[PI_SAT]=1,[PI_DRIVE]=0.08f,
    [PI_COMP_ON]=0,[PI_COMP]=0.0f,[PI_EQB]=0.5f,[PI_EQM]=0.0f,[PI_EQT]=2.0f,
    [PI_CH_MIX]=0.20f,[PI_CH_RATE]=0.9f,[PI_CH_DEPTH]=0.30f,
    [PI_TR_DEPTH]=0.12f,[PI_TR_RATE]=5.5f,[PI_TR_SHAPE]=0,
    [PI_RV_TYPE]=1,[PI_RV_MIX]=0.14f,[PI_RV_DECAY]=0.28f,[PI_RV_DAMP]=0.55f }},

  { "FM5 GRAND PIANO", 23, {
    [PI_INDEX]=1.00f,[PI_DECAY]=1.00f,[PI_HARMONIC]=1.00f,[PI_DETUNE]=0.0f,
    [PI_SUB]=0.0f,[PI_BRIGHT]=0.92f,[PI_SAT]=1,[PI_DRIVE]=0.05f,
    [PI_COMP_ON]=1,[PI_COMP]=0.18f,[PI_EQB]=1.0f,[PI_EQM]=-0.5f,[PI_EQT]=3.0f,
    [PI_CH_MIX]=0.30f,[PI_CH_RATE]=0.45f,[PI_CH_DEPTH]=0.45f,
    [PI_DLY_TYPE]=2,[PI_DLY_MIX]=0.12f,[PI_DLY_MS]=360.0f,[PI_DLY_FB]=0.22f,
    [PI_RV_TYPE]=2,[PI_RV_MIX]=0.24f,[PI_RV_DECAY]=0.55f,[PI_RV_DAMP]=0.35f }}
};
typedef char presets_must_match[(sizeof(PRESETS)/sizeof(PRESETS[0]) == NPRESET) ? 1 : -1];

/* The four newer colour effects reset to their published defaults on a preset
 * change. Sparse overrides here let a few recipes use them deliberately. */
#define C(i) ((i) - P_COLOR_FIRST)
static const float PRESET_COLOR[NPRESET][P_COLOR_LAST - P_COLOR_FIRST + 1] = {
  {},                                                                  /* 0 MK I RHODES */
  { [C(P_FLANGER_MIX)]=0.10f, [C(P_FLANGER_RATE)]=0.30f,
    [C(P_FLANGER_DEPTH)]=0.30f },                                      /* 1 MK II RHODES */
  {},                                                                  /* 2 WURLI 200A */
  {},                                                                  /* 3 CP-70 */
  { [C(P_CRUSH_MIX)]=0.14f, [C(P_CRUSH_BITS)]=10.0f,
    [C(P_CRUSH_RATE)]=18.0f },                                         /* 4 OPL3 E.PIANO */
  {},                                                                  /* 5 NORD STAGE */
  { [C(P_WAH_MIX)]=0.14f, [C(P_WAH_DEPTH)]=0.45f },                    /* 6 SOUL / MOTOWN */
  {},                                                                  /* 7 NEO-SOUL */
  {},                                                                  /* 8 JAZZ ROOM */
  {},                                                                  /* 9 HAMMOND KEYS */
  {},                                                                  /* 10 OPL3 BELL */
  {},                                                                  /* 11 STAGE PIANO */
  {},                                                                  /* 12 ELECTRIC ORGAN */
  {},                                                                  /* 13 VOX CONTINENTAL */
  {},                                                                  /* 14 FARFISA COMPACT */
  { [C(P_RING_MIX)]=0.12f, [C(P_RING_FREQ)]=180.0f,
    [C(P_RING_DEPTH)]=0.35f },                                         /* 15 ACE TONE */
  {},                                                                  /* 16 CHEEZE ORG */
  {},                                                                  /* 17 60S ORGAN */
  {},                                                                  /* 18 PERCUSSIVE ORGAN */
  {}                                                                   /* 19 FM5 GRAND PIANO */
};
#undef C

/* ------------------------------------------------------------------ state ----- */
static float sr = 48000.0f;
static int g_params[P_COUNT];            /* atomic, value x1000 */
static float ui_vals[P_COUNT];           /* control-flow copy */
static int cur_preset;
static int applying;
static int ui_page, help, draw_tick;
static int groove_flash[BF_DRUM_PATTERNS];  /* pad confirmation LEDs, control flow */
static char status[192];
static int status_len, status_pending;
static float level_l, level_r;           /* audio writes, display reads */

/* groove clock, audio-owned; cur_groove_step is published for the display */
static double groove_pos;
static int groove_last_step, groove_bar;
static int cur_groove_step;

/* device pages: four knobs each */
enum {
  UI_SOUND, UI_ENV, UI_FM, UI_TONE, UI_FX, UI_MOD, UI_SPACE, UI_COLOR, UI_SYSTEM,
  UI_GROOVE, UI_GROOVE2, UI_N
};
static const int PAGE_PARAM[UI_N][4] = {
  { P_PRESET, P_VOLUME, P_OCTAVE, P_TRANSPOSE },
  { P_VELOCITY, P_ATTACK, P_RELEASE, P_SPREAD },
  { P_FM_INDEX, P_FM_DECAY, P_FM_FEEDBACK, P_FM_HARMONIC },
  { P_BRIGHT, P_EQ_BASS, P_EQ_MID, P_EQ_TREBLE },
  { P_SAT_TYPE, P_DRIVE, P_COMP_ON, P_COMP_AMT },
  { P_TREM_DEPTH, P_CHORUS_MIX, P_PHASER_MIX, P_ROTARY_ON },
  { P_DELAY_TYPE, P_REVERB_TYPE, P_REVERB_MIX, P_REVERB_DECAY },
  { P_FLANGER_MIX, P_WAH_MIX, P_RING_MIX, P_CRUSH_MIX },
  { P_MASTER, P_VIB_DEPTH, P_VIB_RATE, P_DEMO_RIFF },
  { P_GROOVE_RUN, P_GROOVE_PATTERN, P_GROOVE_BPM, P_GROOVE_SWING },
  { P_GROOVE_SONG, P_GROOVE_HUMANIZE, P_GROOVE_LEVEL, P_GROOVE_TUNE }
};
static const char *const PAGE_NAME[UI_N] = {
  "SOUND", "ENV", "FM", "TONE", "FX", "LFO / MOD", "SPACE", "COLOR", "SYSTEM",
  "GROOVE", "GROOVE 2"
};

static const char *const K_LABELS[6] = {
  "SOUND", "ENV", "LFO", "FX", "SPACE", "DEMO"
};
static const char *const F_LABELS[6] = {
  "PAGE", "NEXT", "AUD", "VOICE", "HELP", "ROTARY"
};

/* control -> audio note messages: SPSC ring, data then release then head */
#define QN 64
static int q_type[QN], q_note[QN], q_vel[QN];
static int q_head, q_tail;

/* Short bossa-nova snippet: a laid-back Ipanema-style turnaround with an
 * original sustained top line (not a transcription of the copyrighted melody).
 * One step is an eighth note; a chord lasts four steps (two beats).
 * Bass: root on beat 1, fifth on beat 3. Chords: upper three voices only, on
 * the "and" of beats 2 and 4. That leaves room for the melody to be heard.
 * Pads 0-7 reuse the full four-note RIFF voicings. */
static const int RIFF[8][4] = {
  { 53, 57, 60, 64 },  /* Fmaj9  */
  { 55, 59, 62, 65 },  /* G13    */
  { 52, 55, 59, 62 },  /* Em9    */
  { 57, 61, 64, 67 },  /* A7     */
  { 50, 53, 57, 60 },  /* Dm9    */
  { 48, 53, 57, 60 },  /* Dm9/C  */
  { 55, 59, 62, 65 },  /* G13    */
  { 48, 52, 55, 59 }   /* Cmaj9  */
};
/* -1 holds the previous note; MEL_DUR says how many eighths it rings. */
static const int MELODY[32] = {
  72, -1, -1, 69,   71, -1, -1, 69,
  67, -1, -1, 66,   64, -1, -1, -1,
  69, -1, -1, 67,   67, -1, -1, 64,
  71, -1, -1, 69,   67, -1, -1, -1
};
static const int MEL_DUR[32] = {
  3, 0, 0, 1,   3, 0, 0, 1,   3, 0, 0, 1,   4, 0, 0, 0,
  3, 0, 0, 1,   3, 0, 0, 1,   3, 0, 0, 1,   4, 0, 0, 0
};
static int riff_clock, riff_step, melody_live = -1, melody_left;
static int riff_live[4];                 /* audio-owned bass/chord notes */
#define DEMO_PATCH 10                    /* fm.c: DEMO TINE, clean 2-op voice */

/* ------------------------------------------------------------------ helpers --- */
static int aget(int *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void aset(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static int scaled(float v) { return (int)(v * 1000.0f + (v >= 0.0f ? 0.5f : -0.5f)); }
static float q1000(int v) { return (float)v * 0.001f; }

/* tiny ASCII appender for the editor status line (no libc formatting needed) */
static int append_txt(char *dst, int at, int cap, const char *src)
{
  int i = 0;
  if (at < 0) at = 0;
  while (src[i] && at + i < cap - 1) { dst[at + i] = src[i]; i++; }
  dst[at + i] = 0;
  return at + i;
}

int barfly_ctrl_atom(int id)
{
  switch (id) {
    case FM_C_INDEX:    return aget(&g_params[P_FM_INDEX]);
    case FM_C_DECAY:    return aget(&g_params[P_FM_DECAY]);
    case FM_C_FEEDBACK: return aget(&g_params[P_FM_FEEDBACK]);
    case FM_C_HARMONIC: return aget(&g_params[P_FM_HARMONIC]);
    case FM_C_DETUNE:   return aget(&g_params[P_FM_DETUNE]) / 1000;
    case FM_C_SUB:      return aget(&g_params[P_SUB]);
    case FM_C_BRIGHT:   return aget(&g_params[P_BRIGHT]);
    case FM_C_ATTACK:   return aget(&g_params[P_ATTACK]);
    case FM_C_RELEASE:  return aget(&g_params[P_RELEASE]);
    case FM_C_VELOCITY: return aget(&g_params[P_VELOCITY]);
    case FM_C_SPREAD:   return aget(&g_params[P_SPREAD]);
    case FM_C_VOLUME:   return aget(&g_params[P_VOLUME]);
    case FM_C_VIB_DEPTH:return aget(&g_params[P_VIB_DEPTH]);
    case FM_C_VIB_RATE: return aget(&g_params[P_VIB_RATE]);
    default: return 0;
  }
}

static void apply_preset(int idx);

static void set_param(int id, float v)
{
  if (id < 0 || id >= P_COUNT) return;
  if (v < PARAMS[id].min) v = PARAMS[id].min;
  if (v > PARAMS[id].max) v = PARAMS[id].max;
  if (PARAMS[id].steps > 1) {
    float st = (PARAMS[id].max - PARAMS[id].min) / (float)(PARAMS[id].steps - 1);
    v = PARAMS[id].min + st * (float)(int)((v - PARAMS[id].min) / st + 0.5f);
  }
  ui_vals[id] = v;
  aset(&g_params[id], scaled(v));
  vibe_param_set(id, v);
  if (id == P_PRESET && !applying) apply_preset((int)(v + 0.5f));
}

static void apply_preset(int idx)
{
  int i;
  if (idx < 0) idx = 0;
  if (idx >= NPRESET) idx = NPRESET - 1;
  applying = 1;
  cur_preset = idx;
  aset(&g_params[P_PRESET], scaled((float)idx));
  ui_vals[P_PRESET] = (float)idx;
  vibe_param_set(P_PRESET, (float)idx);
  fm_set_patch(PRESETS[idx].patch);
  for (i = 0; i < PI_N; i++) set_param(PRESET_IDS[i], PRESETS[idx].v[i]);
  for (i = P_COLOR_FIRST; i <= P_COLOR_LAST; i++) set_param(i, PARAMS[i].def);
  for (i = 0; i <= P_COLOR_LAST - P_COLOR_FIRST; i++)
    if (PRESET_COLOR[idx][i] != 0.0f) set_param(P_COLOR_FIRST + i, PRESET_COLOR[idx][i]);
  applying = 0;
}

static void nudge(int id, float dir)
{
  float v, step;
  if (id < 0 || id >= P_COUNT) return;
  if (PARAMS[id].steps > 1) {
    float st = (PARAMS[id].max - PARAMS[id].min) / (float)(PARAMS[id].steps - 1);
    v = ui_vals[id] + dir * st;
    if (v > PARAMS[id].max) v = PARAMS[id].min;
    if (v < PARAMS[id].min) v = PARAMS[id].max;
  } else {
    float span = PARAMS[id].max - PARAMS[id].min;
    step = span * 0.02f;
    if (step < PARAMS[id].max * 0.001f) step = PARAMS[id].max * 0.001f;
    v = ui_vals[id] + dir * step;
  }
  set_param(id, v);
}

static void store_state(void)
{
  int32_t buf[P_COUNT], i;
  for (i = 0; i < P_COUNT; i++) buf[i] = aget(&g_params[i]);
  vibe_store_write("state-dxtest", buf, (int)sizeof buf);
}

static void load_state(void)
{
  int32_t buf[P_COUNT];
  int n = vibe_store_read("state-dxtest", buf, (int)sizeof buf);
  if (n != (int)sizeof buf) return;
  applying = 1;
  for (int i = 0; i < P_COUNT; i++) {
    float v = (float)buf[i] * 0.001f;
    if (v < PARAMS[i].min) v = PARAMS[i].min;
    if (v > PARAMS[i].max) v = PARAMS[i].max;
    ui_vals[i] = v;
    aset(&g_params[i], buf[i]);
    vibe_param_set(i, v);
  }
  applying = 0;
  cur_preset = (int)(ui_vals[P_PRESET] + 0.5f);
  if (cur_preset < 0) cur_preset = 0;
  if (cur_preset >= NPRESET) cur_preset = NPRESET - 1;
  fm_set_patch(PRESETS[cur_preset].patch);
}

static void post_note(int type, int note, int vel)
{
  int h = q_head, nh = (h + 1) % QN;
  if (nh == q_tail) return;
  q_type[h] = type; q_note[h] = note; q_vel[h] = vel;
  VIBE_RELEASE();
  q_head = nh;
}

static void panic(void)
{
  post_note(3, 0, 0);
}

static void audition(void)
{
  int base = 48 + (int)ui_vals[P_OCTAVE] * 12 + (int)ui_vals[P_TRANSPOSE];
  post_note(1, base, 105);
  post_note(1, base + 7, 90);
  post_note(1, base + 16, 80);
}

/* ------------------------------------------------------------------ events ---- */
static void handle_button(int id)
{
  if (id == VB_F1) { ui_page = (ui_page + 1) % UI_N; return; }
  if (id == VB_F2) { apply_preset((cur_preset + 1) % NPRESET); return; }
  if (id == VB_F3) { audition(); return; }
  if (id == VB_F4) { apply_preset((cur_preset + 1) % NPRESET); return; }
  if (id == VB_F5) { help = !help; return; }
  if (id == VB_F6) {
    set_param(P_ROTARY_ON, q1000(aget(&g_params[P_ROTARY_ON])) > 0.5f ? 0.0f : 1.0f);
    return;
  }
  if (id == VB_K1) { ui_page = UI_SOUND; return; }
  if (id == VB_K2) { ui_page = UI_ENV; return; }
  if (id == VB_K3) { ui_page = UI_MOD; return; }
  if (id == VB_K4) { ui_page = UI_FX; return; }
  if (id == VB_K5) { ui_page = UI_SPACE; return; }
  if (id == VB_K6) {
    if (ui_page == UI_SYSTEM) { ui_page = UI_GROOVE; return; }
    set_param(P_DEMO_RIFF, q1000(aget(&g_params[P_DEMO_RIFF])) > 0.5f ? 0.0f : 1.0f);
    return;
  }
  if (id == VB_REC) { panic(); return; }
  if (id == VB_STOP) {
    set_param(P_DEMO_RIFF, 0.0f);
    set_param(P_GROOVE_RUN, 0.0f);
    return;
  }
  if (id == VB_PLAY) { set_param(P_DEMO_RIFF, 1.0f); return; }
  if (id == VB_RETURN) { help = 0; ui_page = UI_SOUND; return; }
}

static void handle_editor(const uint8_t *b, int n)
{
  if (!b || n < 1) return;
  if (b[0] == 0x20) status_pending = 1;
  else if (b[0] == 0x21) store_state();
  else if (b[0] == 0x22) { apply_preset(0); store_state(); }
  else if (b[0] == 0x23) panic();
  else if (b[0] == 0x24) apply_preset((cur_preset + 1) % NPRESET);
  else if (b[0] == 0x25) apply_preset((cur_preset + NPRESET - 1) % NPRESET);
  else if (b[0] == 0x26 && n >= 2) post_note(1, b[1], 100);
  else if (b[0] == 0x27 && n >= 2) post_note(2, b[1], 0);
  else if (b[0] == 0x40) set_param(P_GROOVE_RUN, ui_vals[P_GROOVE_RUN] > 0.5f ? 0.0f : 1.0f);
  else if (b[0] == 0x41 && n >= 2) bf_drum_pattern_restore((int)b[1] % BF_DRUM_PATTERNS);
  else if (b[0] == 0x42) set_param(P_GROOVE_SONG, ui_vals[P_GROOVE_SONG] > 0.5f ? 0.0f : 1.0f);
}

void vibe_on_event(const vibe_event *e)
{
  int fid = -1, r;
  if (!e) return;
  r = vf_event(e, &fid);
  if (r == VF_PRESS) { if (fid == 900) audition(); else if (fid == 901) panic(); return; }
  if (r == VF_CHANGED) {
    if (fid >= 0 && fid < P_COUNT) set_param(fid, ui_vals[fid]);
    return;
  }
  if (r == VF_MOVED) return;

  if (e->type == VIBE_EV_NOTE) {
    if (e->down) post_note(1, (int)e->id, (int)(e->value * 127.0f + 0.5f));
    else post_note(2, (int)e->id, 0);
    return;
  }
  if (e->type == VIBE_EV_KEY) {
    int note = 48 + (int)e->id;
    if (e->down) post_note(1, note, (int)(e->value * 127.0f + 0.5f));
    else post_note(2, note, 0);
    return;
  }
  if (e->type == VIBE_EV_PAD) {
    int p = (int)e->id, k, nn;
    int oct = (int)ui_vals[P_OCTAVE] * 12;
    if (p < 0 || p >= 16) return;
    if (p >= 8) {                       /* pads 9-16 launch bossa grooves */
      if (e->down) {
        int g = p - 8;
        groove_flash[g] = 10;
        set_param(P_GROOVE_SONG, 0.0f); /* a manual launch leaves song mode */
        set_param(P_GROOVE_PATTERN, (float)g);
        set_param(P_GROOVE_RUN, 1.0f);
      }
      return;
    }
    for (k = 0; k < 4; k++) {           /* chords: the first eight pads */
      nn = RIFF[p][k] + oct;
      post_note(e->down ? 1 : 2, nn, e->down ? 78 : 0);
    }
    return;
  }
  if (e->type == VIBE_EV_PARAM) {
    int i = (int)e->id;
    if (i >= 0 && i < P_COUNT) {
      ui_vals[i] = e->value;
      aset(&g_params[i], scaled(e->value));
      if (i == P_PRESET && !applying) apply_preset((int)(e->value + 0.5f));
    }
    return;
  }
  if (e->type == VIBE_EV_EDITOR_DATA) {
    handle_editor(vibe_editor_data(), (int)e->value); return;
  }
  if (e->type == VIBE_EV_SAVE) { store_state(); return; }
  if (e->type == VIBE_EV_BUTTON) { if (e->down) handle_button((int)e->id); return; }
  if (e->type == VIBE_EV_ENCODER) {
    float d = e->value;
    if (d == 0.0f) return;
    if (e->id == VE_A) { ui_page = (ui_page + (d > 0 ? 1 : UI_N - 1)) % UI_N; return; }
    if (e->id == VE_B) {
      apply_preset((cur_preset + (d > 0 ? 1 : NPRESET - 1)) % NPRESET);
      return;
    }
    if (e->id >= VE_1 && e->id <= VE_4) {
      nudge(PAGE_PARAM[ui_page][e->id - VE_1], d);
      return;
    }
  }
}

/* ------------------------------------------------------------------ audio ----- */
static void fill_fm_ctrl(fm_ctrl *c)
{
  c->index    = aget(&g_params[P_FM_INDEX]);
  c->decay    = aget(&g_params[P_FM_DECAY]);
  c->feedback = aget(&g_params[P_FM_FEEDBACK]);
  c->harmonic = aget(&g_params[P_FM_HARMONIC]);
  c->detune   = aget(&g_params[P_FM_DETUNE]);
  c->sub      = aget(&g_params[P_SUB]);
  c->brightness = aget(&g_params[P_BRIGHT]);
  c->attack   = aget(&g_params[P_ATTACK]);
  c->release  = aget(&g_params[P_RELEASE]);
  c->velocity = aget(&g_params[P_VELOCITY]);
  c->spread   = aget(&g_params[P_SPREAD]);
  c->volume   = aget(&g_params[P_VOLUME]);
  c->vib_depth= aget(&g_params[P_VIB_DEPTH]);
  c->vib_rate = aget(&g_params[P_VIB_RATE]);
}

static void fill_drum_ctrl(bf_drum_ctrl *c)
{
  c->gain  = q1000(aget(&g_params[P_GROOVE_LEVEL]));
  c->tune  = q1000(aget(&g_params[P_GROOVE_TUNE]));
  c->decay = q1000(aget(&g_params[P_GROOVE_DECAY]));
  c->tone  = 0.55f;                    /* closed-hat brightness */
}

/* Per-voice velocities from the feel spec: kick 90-120, rim 60-85, hat 45-90.
 * A deterministic hash per step/voice keeps the "human" spread reproducible. */
static int groove_velocity(int voice, unsigned h)
{
  h += (unsigned)(voice + 1) * 0x9E3779B9u;
  h ^= h >> 7; h *= 0x85EBCA6Bu; h ^= h >> 13;
  if (voice == BF_D_KICK) return 90 + (int)(h % 32);        /* 90-121 */
  if (voice == BF_D_RIM)  return 60 + (int)((h >> 5) % 26);  /* 60-85  */
  if (voice == BF_D_HAT)  return 45 + (int)((h >> 9) % 46);  /* 45-90  */
  return 72 + (int)((h >> 13) % 32);
}

static void fill_fx_amounts(int *a)
{
  a[FX_SAT_TYPE]       = aget(&g_params[P_SAT_TYPE]);
  a[FX_DRIVE]          = aget(&g_params[P_DRIVE]);
  a[FX_COMP_ON]        = aget(&g_params[P_COMP_ON]);
  a[FX_COMP_AMT]       = aget(&g_params[P_COMP_AMT]);
  a[FX_EQ_BASS]        = aget(&g_params[P_EQ_BASS]);
  a[FX_EQ_MID]         = aget(&g_params[P_EQ_MID]);
  a[FX_EQ_TREBLE]      = aget(&g_params[P_EQ_TREBLE]);
  a[FX_TONE]           = aget(&g_params[P_BRIGHT]);
  a[FX_PHASER_MIX]     = aget(&g_params[P_PHASER_MIX]);
  a[FX_PHASER_RATE]    = aget(&g_params[P_PHASER_RATE]);
  a[FX_PHASER_DEPTH]   = aget(&g_params[P_PHASER_DEPTH]);
  a[FX_CHORUS_MIX]     = aget(&g_params[P_CHORUS_MIX]);
  a[FX_CHORUS_RATE]    = aget(&g_params[P_CHORUS_RATE]);
  a[FX_CHORUS_DEPTH]   = aget(&g_params[P_CHORUS_DEPTH]);
  a[FX_TREM_DEPTH]     = aget(&g_params[P_TREM_DEPTH]);
  a[FX_TREM_RATE]      = aget(&g_params[P_TREM_RATE]);
  a[FX_TREM_SHAPE]     = aget(&g_params[P_TREM_SHAPE]);
  a[FX_PAN_DEPTH]      = aget(&g_params[P_PAN_DEPTH]);
  a[FX_PAN_RATE]       = aget(&g_params[P_PAN_RATE]);
  a[FX_ROTARY_ON]      = aget(&g_params[P_ROTARY_ON]);
  a[FX_ROTARY_SPEED]   = aget(&g_params[P_ROTARY_SPEED]);
  a[FX_ROTARY_DEPTH]   = aget(&g_params[P_ROTARY_DEPTH]);
  a[FX_ROTARY_DRIVE]   = aget(&g_params[P_ROTARY_DRIVE]);
  a[FX_ROTARY_BALANCE] = aget(&g_params[P_ROTARY_BALANCE]);
  a[FX_DELAY_TYPE]     = aget(&g_params[P_DELAY_TYPE]);
  a[FX_DELAY_MIX]      = aget(&g_params[P_DELAY_MIX]);
  a[FX_DELAY_MS]       = aget(&g_params[P_DELAY_MS]);
  a[FX_DELAY_FB]       = aget(&g_params[P_DELAY_FB]);
  a[FX_REVERB_TYPE]    = aget(&g_params[P_REVERB_TYPE]);
  a[FX_REVERB_MIX]     = aget(&g_params[P_REVERB_MIX]);
  a[FX_REVERB_DECAY]   = aget(&g_params[P_REVERB_DECAY]);
  a[FX_REVERB_DAMP]    = aget(&g_params[P_REVERB_DAMP]);
  a[FX_FLANGER_MIX]    = aget(&g_params[P_FLANGER_MIX]);
  a[FX_FLANGER_RATE]   = aget(&g_params[P_FLANGER_RATE]);
  a[FX_FLANGER_DEPTH]  = aget(&g_params[P_FLANGER_DEPTH]);
  a[FX_WAH_MIX]        = aget(&g_params[P_WAH_MIX]);
  a[FX_WAH_DEPTH]      = aget(&g_params[P_WAH_DEPTH]);
  a[FX_RING_MIX]       = aget(&g_params[P_RING_MIX]);
  a[FX_RING_FREQ]      = aget(&g_params[P_RING_FREQ]);
  a[FX_RING_DEPTH]     = aget(&g_params[P_RING_DEPTH]);
  a[FX_CRUSH_MIX]      = aget(&g_params[P_CRUSH_MIX]);
  a[FX_CRUSH_BITS]     = aget(&g_params[P_CRUSH_BITS]);
  a[FX_CRUSH_RATE]     = aget(&g_params[P_CRUSH_RATE]);
  a[FX_MASTER]         = aget(&g_params[P_MASTER]);
}

static float drum_l[VIBE_MAX_BLOCK], drum_r[VIBE_MAX_BLOCK];

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  int n = frames, t, type, note, vel;
  fm_ctrl fc;
  bf_drum_ctrl dc;
  int fxv[FX_COUNT];
  float ml = level_l, mr = level_r;
  int demo_on, bpm, period, i, k, tr;
  int groove_on, gpat, song, gbpm;
  float swing, hum, step_sec;
  double steps_per_frame;
  const vibe_transport_info *tp;
  int host_play;
  float vol = q1000(aget(&g_params[P_VOLUME]));
  (void)in;

  if (frames < 1) return;
  if (n > VIBE_MAX_BLOCK) n = VIBE_MAX_BLOCK;

  VIBE_ACQUIRE();
  t = q_tail;
  while (t != q_head) {
    type = q_type[t]; note = q_note[t]; vel = q_vel[t];
    if (type == 1) fm_note_on(note, vel);
    else if (type == 2) fm_note_off(note);
    else if (type == 3) { fm_all_off(); bf_drums_reset(); riff_clock = 0; melody_live = -1; }
    t = (t + 1) % QN;
  }
  q_tail = t;

  fill_fm_ctrl(&fc);
  fill_drum_ctrl(&dc);
  fill_fx_amounts(fxv);

  /* Groove clock: sixteenth steps. A playing DAW leads the tempo and grid;
   * a stopped host leaves the firmware clock running so the pads stay live. */
  tp = vibe_transport();
  host_play = tp && tp->has && tp->playing && tp->bpm > 0.0f;
  groove_on = aget(&g_params[P_GROOVE_RUN]) > 500;
  gpat = aget(&g_params[P_GROOVE_PATTERN]) / 1000;
  song = aget(&g_params[P_GROOVE_SONG]) > 500;
  gbpm = host_play ? (int)tp->bpm : aget(&g_params[P_GROOVE_BPM]) / 1000;
  if (gbpm < 40) gbpm = 40;
  if (gbpm > 240) gbpm = 240;
  step_sec = 60.0f / (float)gbpm / 4.0f;
  swing = q1000(aget(&g_params[P_GROOVE_SWING]));
  hum = q1000(aget(&g_params[P_GROOVE_HUMANIZE]));
  steps_per_frame = (double)gbpm * 4.0 / (60.0 * (double)sr);

  if (!groove_on) {
    groove_pos = 0.0;
    groove_last_step = -1;
    groove_bar = -1;               /* first step arms the song form's bar 0 */
    __atomic_store_n(&cur_groove_step, -1, __ATOMIC_RELEASE);
  } else if (host_play) {
    double want = tp->beat * 4.0;
    if (want < 0.0) want = 0.0;
    if (want - groove_pos > 0.25 || groove_pos - want > 0.25) {
      groove_pos = want;
      groove_last_step = -1;
      groove_bar = 0;
    }
  }

  demo_on = aget(&g_params[P_DEMO_RIFF]);
  bpm = aget(&g_params[P_DEMO_BPM]) / 1000;
  if (bpm < 40) bpm = 40;
  if (bpm > 240) bpm = 240;
  period = (int)(sr * 30.0f / (float)bpm);   /* one eighth note per step */

  for (i = 0; i < n; i++) {
    if (demo_on) {
      riff_clock++;
      if (riff_clock >= period) {
        int oct = aget(&g_params[P_OCTAVE]) / 1000;
        int tr = aget(&g_params[P_TRANSPOSE]) / 1000;
        int step = riff_step & 31;
        int chord = (riff_step >> 2) & 7;
        int pos = step & 3;                   /* eighth within the chord */
        int live = 0, nn;
        riff_clock = 0;
        for (k = 0; k < 4; k++) if (riff_live[k] >= 0) fm_note_off(riff_live[k]);
        for (k = 0; k < 4; k++) riff_live[k] = -1;

        if (pos == 0) {                       /* root on beat 1 */
          nn = RIFF[chord][0] - 12 + oct * 12 + tr;
          riff_live[live++] = nn;
          fm_note_on_patch(nn, 72, DEMO_PATCH);
        } else if (pos == 2) {                /* fifth on beat 3 */
          nn = RIFF[chord][0] - 5 + oct * 12 + tr;
          riff_live[live++] = nn;
          fm_note_on_patch(nn, 66, DEMO_PATCH);
        }
        if (pos == 3) {                       /* upper voices on the "and" */
          for (k = 1; k < 4; k++) {
            nn = RIFF[chord][k] + oct * 12 + tr;
            riff_live[live++] = nn;
            fm_note_on_patch(nn, 36, DEMO_PATCH);
          }
        }

        if (melody_live >= 0 && --melody_left <= 0) {
          fm_note_off(melody_live); melody_live = -1;
        }
        nn = MELODY[step];                    /* sustained original top line */
        if (nn >= 0) {
          if (melody_live >= 0) fm_note_off(melody_live);
          melody_live = nn + oct * 12 + tr;
          melody_left = MEL_DUR[step];
          fm_note_on_patch(melody_live, 108, DEMO_PATCH);
        }
        riff_step = (riff_step + 1) & 255;
      }
    }

    if (groove_on) {
      int s = (int)groove_pos;
      double ph = groove_pos - (double)s;
      int st = ((s % BF_DRUM_STEPS) + BF_DRUM_STEPS) % BF_DRUM_STEPS;
      double late = 0.008 / (double)step_sec;   /* 8 ms baseline for +/- humanize */
      unsigned h;

      if (st & 1) late += (double)swing * 0.35; /* laid-back sixteenths */
      if (hum > 0.0f) {
        h = (unsigned)s * 2246822519u + 0x9E3779B9u;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
        late += (((double)(h & 255) / 255.0) * 2.0 - 1.0) *
                0.008 * (double)hum / (double)step_sec;
      }
      if (s != groove_last_step) {
        int bar = ((s / BF_DRUM_STEPS) % 8 + 8) % 8;
        if (song && bar != groove_bar) {
          groove_bar = bar;
          gpat = bf_drum_song_pattern(bar);
        }
      }
      if (s != groove_last_step && ph >= late) {
        unsigned char mask = bf_drum_step(gpat, st);
        groove_last_step = s;
        __atomic_store_n(&cur_groove_step, st, __ATOMIC_RELEASE);
        h = (unsigned)s * 2654435761u + (unsigned)st + 0x9E3779B9u;
        for (tr = 0; tr < BF_D_COUNT; tr++)
          if (mask & (1u << tr))
            bf_drums_trigger(tr, groove_velocity(tr, h));
      }
      groove_pos += steps_per_frame;
    }

    out_l[i] = 0.0f;
    out_r[i] = 0.0f;
  }

  fm_render(out_l, out_r, n, &fc);
  for (i = 0; i < n; i++) { out_l[i] *= vol; out_r[i] *= vol; }
  bf_drums_render(drum_l, drum_r, n, &dc);
  for (i = 0; i < n; i++) { out_l[i] += drum_l[i]; out_r[i] += drum_r[i]; }
  fx_process(out_l, out_r, n, fxv);

  for (i = 0; i < n; i++) {
    float al = out_l[i] < 0 ? -out_l[i] : out_l[i];
    float ar = out_r[i] < 0 ? -out_r[i] : out_r[i];
    ml = al > ml ? al : ml * 0.9992f;
    mr = ar > mr ? ar : mr * 0.9992f;
  }
  level_l = ml; level_r = mr;

  for (i = n; i < frames; i++) { out_l[i] = 0.0f; out_r[i] = 0.0f; }
}

/* ------------------------------------------------------------------ display --- */
#define C_BG      0x140D0A
#define C_PANEL   0x241811
#define C_PANEL2  0x36241A
#define C_ACC     0xFF8C1A
#define C_GOLD    0xF2C14E
#define C_CREAM   0xF3E5C8
#define C_AVO     0x8FA44B
#define C_RED     0xC94E3F
#define C_MUTED   0x947D6B

static void draw_fly(float cx, float cy, float flap)
{
  float w = 6.0f + 4.0f * flap;
  vg_tri(cx - 6, cy - 1, cx - 24 - w, cy - 18 - w, cx - 26 - w, cy + 2, C_GOLD);
  vg_tri(cx + 6, cy - 1, cx + 24 + w, cy - 18 - w, cx + 26 + w, cy + 2, C_GOLD);
  vg_circle(cx - 16, cy - 8, 3.0f, C_CREAM);
  vg_circle(cx + 16, cy - 8, 3.0f, C_CREAM);
  vg_circle(cx, cy + 2, 10.0f, C_ACC);
  vg_circle(cx, cy + 6, 7.0f, C_BG);
  vg_circle(cx, cy + 11, 5.5f, C_ACC);
  vg_circle(cx - 3, cy - 11, 5.0f, C_CREAM);
  vg_circle(cx + 3, cy - 11, 5.0f, C_CREAM);
  vg_rect(cx - 8, cy - 13, 16, 4, C_BG);
  vg_rect(cx - 1, cy - 12, 2, 3, C_GOLD);
  vg_line(cx - 4, cy - 20, cx - 9, cy - 27, 1.0f, C_CREAM);
  vg_line(cx + 4, cy - 20, cx + 9, cy - 27, 1.0f, C_CREAM);
}

static const char *const HELP[] = {
  "KEYS", "play the electric piano",
  "PADS", "1-8 chords, 9-16 bossa grooves",
  "ENC A / F1", "next display page",
  "ENC B / F2", "next factory voice",
  "KNOBS 1-4", "edit the four values shown",
  "F3", "audition a chord",
  "F4", "next factory voice",
  "F5 / HOME", "this overview",
  "F6", "Leslie on / off",
  "K1", "SOUND page",
  "K2", "ENV page",
  "K3", "LFO / MOD page",
  "K4", "FX page",
  "K5", "SPACE page",
  "K6", "bossa snippet / groove page",
  "DATA + ENTER", "FOCUS: walk, press to edit",
  "REC", "panic / all notes off",
  "STOP / PLAY", "stop all / bossa snippet on",
  "RETURN", "back to SOUND"
};

static void build_status(void)
{
  int n = 0;
  char t[24];
  status[0] = 0;
  n = append_txt(status, n, (int)sizeof status, PRESETS[cur_preset].name);
  n = append_txt(status, n, (int)sizeof status, " | voices ");
  n = append_txt(status, n, (int)sizeof status, vg_itoa(fm_voice_count(), t));
  n = append_txt(status, n, (int)sizeof status, " | rotor ");
  n = append_txt(status, n, (int)sizeof status,
                 fx_rotor_hz100() > 20 ? "FAST" : (fx_rotor_hz100() > 0 ? "SLOW" : "STOP"));
  n = append_txt(status, n, (int)sizeof status, " | ");
  n = append_txt(status, n, (int)sizeof status,
                 aget(&g_params[P_DEMO_RIFF]) ? "bossa on" : "bossa off");
  n = append_txt(status, n, (int)sizeof status, " | groove ");
  n = append_txt(status, n, (int)sizeof status,
                 aget(&g_params[P_GROOVE_RUN])
                   ? bf_drum_pattern_name((int)(ui_vals[P_GROOVE_PATTERN] + 0.5f))
                   : "off");
  status_len = n;
  status_pending = 1;
}

void vibe_on_draw(uint16_t *fb)
{
  int i, id;
  float flap;
  vg_begin(fb);
  if (help) { vg_controls("BAR-FLY CONTROLS", HELP, (int)(sizeof HELP / sizeof HELP[0]) / 2); return; }

  vg_clear(C_BG);
  vg_rect(0, 0, 480, 44, C_PANEL);
  vg_rect(0, 43, 480, 2, C_ACC);
  draw_fly(452, 22, 0.5f + 0.5f * vd_sin((float)draw_tick * 0.06f));
  vg_text("BAR-FLY", 14, 8, VG_BIG, C_GOLD, VG_LEFT);
  vg_text(PRESETS[cur_preset].name, 16, 28, VG_SMALL, C_CREAM, VG_LEFT);
  vg_text(PAGE_NAME[ui_page], 400, 14, VG_BOLD, C_ACC, VG_RIGHT);

  vf_begin();
  for (i = 0; i < 4; i++) {
    int y = 58 + i * 47;
    char val[24];
    float t;
    id = PAGE_PARAM[ui_page][i];
    vg_round(10, y, 460, 42, 5, i & 1 ? C_PANEL : C_PANEL2);
    vg_text(PARAMS[id].name, 22, y + 4, VG_SMALL, C_MUTED, VG_LEFT);
    if (PARAMS[id].steps > 1) {
      int k = (int)(ui_vals[id] + 0.5f);
      if (k < 0) k = 0;
      if (k >= PARAMS[id].steps) k = PARAMS[id].steps - 1;
      vg_text(PARAMS[id].choices[k], 458, y + 3, VG_MED, C_CREAM, VG_RIGHT);
    } else {
      int dec = (PARAMS[id].max - PARAMS[id].min) <= 3.0f ? 2 : 1;
      vg_text(vg_ftoa(ui_vals[id], dec, val), 458, y + 3, VG_BIG, C_CREAM, VG_RIGHT);
    }
    t = (ui_vals[id] - PARAMS[id].min) / (PARAMS[id].max - PARAMS[id].min);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    vg_rect(22, y + 30, 420, 5, C_BG);
    vg_rect(22, y + 30, (int)(420.0f * t), 5, C_ACC);
    vf_value(id, 10, y, 460, 42, &ui_vals[id], PARAMS[id].min,
             PARAMS[id].max, (PARAMS[id].max - PARAMS[id].min) / 100.0f);
  }

  /* 16-step bossa groove grid */
  {
    int st = aget(&cur_groove_step);
    int pat = (int)(ui_vals[P_GROOVE_PATTERN] + 0.5f);
    if (pat < 0) pat = 0;
    if (pat >= BF_DRUM_PATTERNS) pat = BF_DRUM_PATTERNS - 1;
    for (i = 0; i < BF_DRUM_STEPS; i++) {
      int x = 14 + i * 28;
      int hit = bf_drum_step(pat, i) != 0;
      vg_round(x, 247, 24, 10, 2, i == st ? C_ACC : (hit ? C_GOLD : C_PANEL2));
    }
    vg_text(bf_drum_pattern_name(pat), 470, 246, VG_SMALL, C_MUTED, VG_RIGHT);
  }

  /* level meters and voice count */
  {
    char val[24];
    float ml = level_l, mr = level_r;
    if (ml > 1.0f) ml = 1.0f;
    if (mr > 1.0f) mr = 1.0f;
    vg_text("L", 14, 264, VG_SMALL, C_MUTED, VG_LEFT);
    vg_rect(28, 266, 180, 7, C_PANEL2);
    vg_rect(28, 266, (int)(180.0f * ml), 7, ml > 0.85f ? C_RED : C_AVO);
    vg_text("R", 222, 264, VG_SMALL, C_MUTED, VG_LEFT);
    vg_rect(236, 266, 180, 7, C_PANEL2);
    vg_rect(236, 266, (int)(180.0f * mr), 7, mr > 0.85f ? C_RED : C_AVO);
    vg_text("VOICES", 470, 264, VG_SMALL, C_MUTED, VG_RIGHT);
    vg_text(vg_itoa(fm_voice_count(), val), 470, 274, VG_BOLD, C_GOLD, VG_RIGHT);
    vf_button(900, 416, 40, 50, 28);
    vf_button(901, 416, 64, 50, 28);
  }

  vg_rect(0, 282, 480, 2, C_ACC);
  vg_rect(0, 284, 480, 36, C_PANEL);
  for (i = 0; i < 6; i++) {
    vg_text(F_LABELS[i], 8 + i * 79, 286, VG_SMALL,
            i == 1 ? C_GOLD : C_MUTED, VG_LEFT);
    vf_key(VB_F1 + i, 4 + i * 79, 283, 76, 18);
  }
  for (i = 0; i < 6; i++) {
    vg_text(K_LABELS[i], 10 + i * 79, 304, VG_SMALL, C_AVO, VG_LEFT);
    vf_key(VB_K1 + i, 4 + i * 79, 301, 76, 18);
  }
  vf_end(C_ACC);

  /* pad LEDs: lower pads are chords, upper pads launch the eight grooves */
  {
    int pat = (int)(ui_vals[P_GROOVE_PATTERN] + 0.5f);
    if (pat < 0) pat = 0;
    if (pat >= BF_DRUM_PATTERNS) pat = BF_DRUM_PATTERNS - 1;
    for (i = 0; i < 16; i++) {
      uint32_t c = C_PANEL2;
      float lvl = 0.0f;
      if (i >= 8 && groove_flash[i - 8] > 0) {
        c = C_AVO;
        lvl = (float)groove_flash[i - 8] / 10.0f;
        groove_flash[i - 8]--;
      } else if (i >= 8 && i - 8 == pat) {
        c = C_AVO;
        lvl = aget(&g_params[P_GROOVE_RUN]) ? 0.55f : 0.25f;
      }
      vibe_led_pad(i, c, lvl);
    }
    vibe_led_button(VB_PLAY, aget(&g_params[P_DEMO_RIFF]) ? C_AVO : C_PANEL2, 0.7f);
  }

  for (i = 0; i < 6; i++) vibe_label(VB_F1 + i, F_LABELS[i]);
  for (i = 0; i < 6; i++) vibe_label(VB_K1 + i, K_LABELS[i]);
  vibe_link_page(PAGE_NAME[ui_page]);
  for (i = 0; i < 4; i++) {
    int id = PAGE_PARAM[ui_page][i];
    float t = (ui_vals[id] - PARAMS[id].min) /
              (PARAMS[id].max - PARAMS[id].min);
    char lv[24];
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    vg_ftoa(t, 2, lv);
    vibe_link_encoder(VE_1 + i, PARAMS[id].name, lv);
  }

  draw_tick++;
  if ((draw_tick % 8) == 0) build_status();
  if (status_pending) {
    if (vibe_editor_post((const uint8_t *)status, (int)status_len) >= 0)
      status_pending = 0;
  }
  (void)flap;
}

/* ------------------------------------------------------------------ init ------ */
void vibe_on_init(float sample_rate)
{
  int i;
  sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
  fm_init(sr);
  bf_drums_init(sr);
  fx_init(sr);

  for (i = 0; i < P_COUNT; i++) {
    ui_vals[i] = PARAMS[i].def;
    aset(&g_params[i], scaled(PARAMS[i].def));
  }
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_editor_cards(EDITOR_CARDS, (int)(sizeof(EDITOR_CARDS) / sizeof(EDITOR_CARDS[0])));
  apply_preset(0);
  load_state();

  vibe_editor_accent(C_ACC);
  vibe_editor_tab_color("SOUND", C_ACC);
  vibe_editor_tab_color("FM", C_GOLD);
  vibe_editor_tab_color("TONE", C_RED);
  vibe_editor_tab_color("FX", 0x5AB6FF);
  vibe_editor_tab_color("SPACE", C_AVO);
  vibe_editor_tab_color("LFO", 0xB987FF);
  vibe_editor_tab_color("SYSTEM", C_CREAM);
  vibe_editor_tab_color("GROOVE", C_AVO);

  vibe_editor_control(P_GROOVE_RUN, VIBE_CTL_TOGGLE);
  vibe_editor_control(P_GROOVE_SONG, VIBE_CTL_TOGGLE);
  vibe_editor_control(P_GROOVE_PATTERN, VIBE_CTL_MENU);
  vibe_editor_control(P_DEMO_RIFF, VIBE_CTL_TOGGLE);

  vibe_param_help(P_FM_INDEX, "DX-style modulation index. More means a brighter, harder tine attack.");
  vibe_param_help(P_FM_DECAY, "Time scale of the operator envelopes: low is a quick pluck, high is a long tone.");
  vibe_param_help(P_TREM_DEPTH, "Amplitude LFO depth - the classic Rhodes tremolo.");
  vibe_param_help(P_CHORUS_MIX, "Stereo chorus: two slowly modulated delay taps, one per channel.");
  vibe_param_help(P_PHASER_MIX, "Four-stage phaser, the Small Stone / Phase 90 colour. On by default for SOUL / MOTOWN.");
  vibe_param_help(P_ROTARY_ON, "Dual-rotor Leslie: independent horn/drum motors and stereo mics. Off on HAMMOND KEYS; F6 toggles.");
  vibe_param_help(P_REVERB_TYPE, "Room, Hall, Plate or Spring reverb character.");
  vibe_param_help(P_DELAY_TYPE, "Ping-pong stereo echo or damped tape echo with flutter.");
  vibe_param_help(P_FLANGER_MIX, "Short modulated delay with feedback; a jet-like sweep for electric piano.");
  vibe_param_help(P_WAH_MIX, "Envelope-following bandpass sweep; the classic talking-wah colour.");
  vibe_param_help(P_RING_MIX, "Ring modulation: a sine multiplier for bell-like sidebands.");
  vibe_param_help(P_CRUSH_MIX, "Sample-and-hold decimation with reduced bit depth for lo-fi chip colour.");
  vibe_param_help(P_DEMO_RIFF, "Short bossa-nova snippet: bass, syncopated chords and an original top line over an Ipanema-style turnaround.");
  vibe_param_help(P_GROOVE_RUN, "PLAY runs the selected bossa groove. Pads 9-16 launch one directly; STOP silences everything.");
  vibe_param_help(P_GROOVE_PATTERN, "Eight 16-step grooves: basic, anticipated, lounge, syncopated, jazz, hotel, samba and loose ending.");
  vibe_param_help(P_GROOVE_SONG, "8-bar form: 1, 2, 3, 4, 1, 5, 7, 8. Pressing a pad returns to single-groove play.");
  vibe_param_help(P_GROOVE_SWING, "Laid-back shuffle, 55-58 percent by default: delays the off-beat sixteenths.");
  vibe_param_help(P_GROOVE_HUMANIZE, "Deterministic timing spread of up to plus/minus 8 ms, for a live-player feel.");

  vibe_notes(VIBE_NOTES);
  vibe_keys_octave(2);
  vf_mode(VF_ON_HOLD);
  vf_clicks(1);
  vf_set_focus(P_VOLUME);

  q_head = q_tail = 0;
  riff_clock = 0; riff_step = 0; melody_live = -1; melody_left = 0;
  for (i = 0; i < 4; i++) riff_live[i] = -1;
  for (i = 0; i < BF_DRUM_PATTERNS; i++) groove_flash[i] = 0;
  groove_pos = 0.0;
  groove_last_step = -1;
  groove_bar = 0;
  __atomic_store_n(&cur_groove_step, -1, __ATOMIC_RELEASE);
  ui_page = UI_SOUND;
  help = 0;
  draw_tick = 0;
  build_status();
}
