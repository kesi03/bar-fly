/* bar-fly FM engine -- six operators, DX7/Dexed-style stacks.
 *
 * Operator ordering is "modulator first": an operator that modulates another
 * one always has the lower index. That makes one pass over the six operators
 * per sample enough, with no recursion and no per-sample allocation.
 *
 * The patch recipes follow the classic DX7 electric-piano approach (see the
 * EMU E.PNO / EMU BELL voices in the FM-1 emulator's Dexed-derived bank):
 *   - a 1:1 stack gives the tine body (fast-decaying index, slower carrier)
 *   - a high-ratio stack (7:1 / 14:1) gives the bell/tine sparkle
 *   - all brightness movement comes from operator envelopes, not filters
 *
 * Audio flow only. The patch request is a single atomic int; voices copy the
 * whole patch at note-on so a patch change never disturbs a ringing note.
 */
#include "vibe_dsp.h"
#include "fm.h"
#include "opl3.h"

/* Raw Nuked OPL3 register recipes, one per BAR-FLY voice.
 * Operator fields: mult, tl, ar, dr, sl, rr, wave.
 * wavforms: 0 sine, 1 half-sine, 2 abs-sine, 3 quarter-pulse.
 * A high modulator SL lets the brightness die away; the carrier holds on. */
static const opl3_patch_def BF_BAR_FLY = {
  1, { { {1, 32, 15, 9, 12, 2, 0}, {1, 8, 15, 4, 3, 2, 0} } }, { 5 }, { 0 }
};
/* Rhodes: channel one is the 1:1 tine with a slow-decaying index and a
 * long fundamental; channel two adds the 2:1 / 3:1 bell partial that makes
 * an electric piano read as a tine instead of a plain sine. Backed by the
 * Nuked OPL3 envelope calibration probe (DR 0 + SL 15 = flat sustain,
 * DR 1/2/3/4 = progressively faster decay to silence). */
static const opl3_patch_def BF_MK1 = {
  2, {
    { {1,38,15,3,15,4,0,0x11,0}, {1,0,15,1,15,7,0,0x11,0} },
    { {2,46,15,4,15,3,0,0x12,0}, {1,16,15,2,15,5,0,0x11,0} }
  },
  { 2, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
};
static const opl3_patch_def BF_MK2 = {
  2, {
    { {1,42,15,3,15,4,0,0x11,0}, {1,8,15,1,15,7,0,0x11,0} },
    { {3,54,15,4,15,3,0,0x13,0}, {1,26,15,2,15,5,0,0x11,0} }
  },
  { 4, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
};
/* Wurlitzer: no GM Wurlitzer record exists, so this layers two real bank
 * voices: "Chorused Piano" from GENMIDI organ18.wopl program 5 (an octave-up
 * reed/chorus layer) over EPIANO2 from test-2.wopl program 5 (the tine body). */
static const opl3_patch_def BF_WURLI = {
  2, {
    { {0,17,15,1,14,6,0,48,1}, {0,0,15,1,14,5,0,176,1} },
    { {10,53,10,4,8,14,0,58,0}, {1,7,10,1,8,5,0,49,0} }
  },
  { 3, 6 }, { 0, 0 },
  { 12, 0 }, { 0, 0 }
};
static const opl3_patch_def BF_CP70 = {
  1, { { {2, 36, 15, 8, 11, 2, 1}, {1, 10, 15, 5, 4, 2, 0} } }, { 2 }, { 0 }
};
static const opl3_patch_def BF_DX7 = {
  1, { { {1, 30, 15, 8, 12, 2, 0}, {1, 9, 15, 4, 3, 2, 0} } }, { 6 }, { 0 }
};
/* Three distinct electric pianos. Nord is a clean two-channel sine stack with
 * a long body and a quiet 3x partial; Soul is a faster, grittier abs-sine tine
 * with a 3x bell layer. Both keep the second channel additive so the voices do
 * not collapse into the same 1:1 FM sine as the old presets 5-7. */
static const opl3_patch_def BF_NORD = {
  2, {
    { {1,40,15,3,15,6,0,0x11,0}, {1,3,15,1,15,7,0,0x11,0} },
    { {3,34,15,4,15,5,0,0x13,0}, {1,20,15,2,15,6,0,0x11,0} }
  },
  { 1, 0 }, { 0, 1 },
  { 0, 0 }, { 0, -2 }
};
static const opl3_patch_def BF_SOUL = {
  2, {
    { {1,40,15,4,15,3,2,0x11,0}, {1,6,15,2,15,4,0,0x11,0} },
    { {2,50,15,5,15,2,0,0x12,0}, {1,16,15,3,15,3,0,0x11,0} }
  },
  { 4, 0 }, { 0, 0 },
  { 0, 0 }, { 0, 4 }
};
static const opl3_patch_def BF_NEO = {
  1, { { {2, 34, 15, 9, 12, 2, 0}, {1, 9, 15, 5, 4, 2, 0} } }, { 3 }, { 0 }
};
/* Hammond: four clean sine drawbars - 16', 8', 4', 2' (0.5/1/2/4x) - with
 * flat full sustain and fast attack. The previous hybrid used half-sine
 * waveforms and a duplicate 8' partial, which read as a generic combo organ
 * rather than a tonewheel drawbar stack. Still four real OPL3 operators. */
static const opl3_patch_def BF_HAMMOND = {
  2, {
    { {0,22,15,0,15,4,0,0x10,0}, {1,10,15,0,15,4,0,0x11,0} },
    { {2,20,15,0,15,4,0,0x12,0}, {4,26,15,0,15,4,0,0x14,0} }
  },
  { 0, 0 }, { 1, 1 },
  { 0, 0 }, { 0, 0 }
};
static const opl3_patch_def BF_DEMO = {
  1, { { {1, 44, 15, 9, 13, 2, 0}, {1, 10, 15, 6, 6, 2, 0} } }, { 2 }, { 0 }
};
static const opl3_patch_def BF_OPL3_EPIANO = {
  1, { { {1, 30, 15, 8, 12, 2, 1}, {1, 8, 15, 4, 4, 2, 0} } }, { 5 }, { 0 }
};
static const opl3_patch_def BF_OPL3_BELL = {
  1, { { {3, 26, 15, 10, 14, 2, 2}, {1, 12, 15, 5, 2, 2, 0} } }, { 3 }, { 0 }
};
/* Stage piano: the DMXOPL3 "Warm Grand Piano" record kept the modulator at
 * full index and put tremolo/vibrato on it, which reads as an organ. This is
 * the same 2x2op OPL3 structure with a 1:1 body (long fundamental, decaying
 * index) and a 2:1 upper partial, all sine waves and no LFO bits. */
static const opl3_patch_def BF_PIANO = {
  2, {
    { {1,38,15,3,15,4,0,0x11,0}, {1,4,15,1,15,6,0,0x11,0} },
    { {2,44,15,4,15,3,0,0x12,0}, {1,10,15,2,15,4,0,0x11,0} }
  },
  { 0, 0 }, { 0, 0 },
  { 0, 0 }, { 0, 0 }
};
/* Vibraphone: an OPL3 bar recipe, not a sampled instrument. Channel one gives
 * the fundamental plus the characteristic 4th partial (two octaves up);
 * channel two adds the quieter 3rd and 10th partials. All four operators are
 * sine waves with long, gentle decays and no LFO bits. The rotating-bar
 * tremolo is handled by the FX rack, so the voice itself stays in tune. */
static const opl3_patch_def BF_VIBE = {
  2, {
    { {1,2,15,0,15,7,0,0x11,0}, {4,12,15,1,15,6,0,0x14,0} },
    { {3,26,15,2,15,5,0,0x13,0}, {10,30,15,3,15,4,0,0x1a,0} }
  },
  { 0, 0 }, { 1, 1 },
  { 0, 0 }, { 0, 0 }
};
/* Electric organ: reedier drawbars (1,2,4,6') with a slow release. */
static const opl3_patch_def BF_EORGAN = {
  2, {
    { {1, 18, 14, 0, 0, 1, 0}, {2, 26, 14, 0, 0, 1, 1} },
    { {4, 34, 14, 0, 0, 1, 3}, {6, 40, 14, 0, 0, 1, 3} }
  },
  { 0, 0 }, { 1, 1 }
};
/* Vox Continental: thin, flutey drawbars with the characteristic 1 3/5' edge. */
static const opl3_patch_def BF_VOX = {
  2, {
    { {1, 18, 15, 0, 0, 2, 0}, {2, 26, 15, 0, 0, 2, 2} },
    { {3, 34, 15, 0, 0, 2, 1}, {5, 40, 15, 0, 0, 2, 3} }
  },
  { 0, 0 }, { 1, 1 }
};
/* Farfisa Compact: fat, buzzy Italian combo organ with strong 2nd/4th/6th. */
static const opl3_patch_def BF_FARFISA = {
  2, {
    { {1, 22, 14, 0, 0, 2, 0}, {2, 28, 14, 0, 0, 2, 3} },
    { {4, 34, 14, 0, 0, 2, 1}, {6, 40, 14, 0, 0, 2, 3} }
  },
  { 0, 0 }, { 1, 1 }
};
/* Ace Tone: early combo organ -- a soft FM body plus 2nd/3rd drawbars. */
static const opl3_patch_def BF_ACETONE = {
  2, {
    { {1, 32, 15, 0, 0, 2, 1}, {1, 10, 15, 0, 0, 2, 0} },
    { {2, 36, 15, 0, 0, 2, 0}, {3, 44, 15, 0, 0, 2, 2} }
  },
  { 2, 0 }, { 0, 1 }
};
/* Cheeze Org: bright two-channel XG drawbar organ from DMXOPL GENMIDI(XG).wopl
 * bank 16 program 16, transcribed from the raw WOPL register record. */
static const opl3_patch_def BF_DMX_CHEEZE = {
  2, {
    { {0,4,10,10,1,8,0,112,1}, {1,16,8,10,0,8,0,177,0} },
    { {1,6,10,10,1,8,3,113,0}, {2,16,8,10,0,8,0,178,0} }
  },
  { 2, 0 }, { 2, 0 },
  { 12, 19 }, { 0, 10 }
};
/* 60's Organ 1: bright two-channel combo organ from DMXOPL GENMIDI(GS).wopl
 * bank 2 program 16, transcribed from the raw WOPL register record. Channel
 * one is a feedback FM body; channel two is an octave-up additive portion. */
static const opl3_patch_def BF_DMX_60S = {
  2, {
    { {4,8,11,1,1,8,0,164,2}, {1,0,13,1,1,11,0,97,2} },
    { {3,8,11,1,0,8,0,163,2}, {1,8,13,1,1,11,0,97,2} }
  },
  { 3, 1 }, { 2, 1 },
  { 0, 12 }, { 0, 3 }
};
/* Chip E.Piano: the unassigned patch 21 voice. A half-sine FM bite over a long
 * sine body, plus a 2x abs-sine channel for the "sound-card" edge. */
static const opl3_patch_def BF_CHIP_EP = {
  2, {
    { {1,36,15,3,15,4,1,0x11,0}, {1,8,15,1,15,5,0,0x11,0} },
    { {2,48,15,4,15,4,2,0x12,0}, {1,18,15,2,15,5,0,0x11,0} }
  },
  { 5, 2 }, { 0, 1 },
  { 0, 0 }, { 0, 3 }
};

/* Direct WOPL transcriptions. The nine operator fields are
 * mult, tl, ar, dr, sl, rr, wave, raw 0x20 avekm, and 0x40 key-scale bits.
 * Avekm/KSR are written through so the bank's EG/KSR behaviour is preserved. */
static const opl3_patch_def BF_PERC_ORG = {
  1, {
    { {0,0,15,1,0,5,1,48,0}, {4,0,15,4,3,6,0,148,0} },
    { {0,0,0,0,15,0,0,0,0}, {0,0,0,0,15,0,0,0,0} }
  },
  { 3, 0 }, { 1, 0 },
  { 0, 12 }, { 0, 0 }
}; /* organ18 program 17 Percussive Organ */
static const opl3_patch_def BF_FM5_GRAND = {
  2, {
    { {5,45,14,1,2,3,0,181,2}, {1,0,15,1,15,4,0,177,0} },
    { {2,38,15,1,2,3,0,178,0}, {1,0,15,1,15,4,0,177,0} }
  },
  { 7, 7 }, { 0, 0 },
  { 0, 0 }, { 0, 2 }
}; /* Organ 24 program 5 FM5 Grand Piano */

#define OP(R,D,L,A,DC,S,RL,DEST,FB)      { R,D,L,A,DC,S,RL,DEST,FB,0 }
#define OPW(R,D,L,A,DC,S,RL,DEST,FB,W)   { R,D,L,A,DC,S,RL,DEST,FB,W }

/* All patches: ratio x100, detune cents, level 0..100, times in ms,
 * sustain x100, destination (-1 = output), self-feedback x100. */
static const fm_patch PATCHES[] = {
  { "BAR FLY", 6, {
    OP(100,   0, 58,  1, 110,   0, 180,  1, 0),   /* tine index        */
    OP(100,   0, 96,  3, 900,  62, 650, -1, 0),   /* tine carrier      */
    OP(700,   6, 26,  1,  70,   0, 130,  3, 0),   /* bell index        */
    OP(100,  -5, 62,  2, 620,  45, 520, -1, 0),   /* bell carrier      */
    OP(1400,  0, 12,  1,  45,   0,  90,  5, 0),   /* top sparkle index */
    OP(100,   9, 26,  2, 380,  18, 360, -1, 0)    /* top sparkle carrier */
  }, &BF_BAR_FLY },
  { "MK I RHODES", 4, {
    OP(100,   0, 55,  1,  95,   0, 170,  1, 0),
    OP(100,   0, 96,  3, 950,  70, 700, -1, 0),
    OP(700,  -4, 20,  1,  55,   0, 110,  3, 0),
    OP(100,   5, 52,  2, 700,  52, 560, -1, 0)
  }, &BF_MK1 },
  { "MK II RHODES", 5, {
    OP(200,   0, 68,  1,  75,   0, 140,  1, 0),
    OP(100,   0, 95,  2, 720,  52, 640, -1, 0),
    OP(100,   7, 54,  2, 100,   0, 160,  3, 0),
    OP(100,  -7, 70,  2, 780,  58, 620, -1, 0),
    OP(1400,  0, 22,  1,  40,   0,  80,  3, 0)
  }, &BF_MK2 },
  { "WURLI 200A", 5, {
    OP(200,   0, 38,  2, 260,   8, 220,  1, 0),   /* reed buzz         */
    OP(100,   0, 92,  1, 1200, 78, 480, -1, 0),
    OP(300,  -3, 22,  2, 420,   0, 180,  3, 0),
    OP(100,   4, 48,  1, 900,  62, 520, -1, 0),
    OP(400,   0, 10,  1, 180,   0, 120,  3, 0)
  }, &BF_WURLI },
  { "CP-70", 6, {
    OP(200,   0, 44,  1, 180,   0, 200,  1, 0),
    OP(100,   0, 90,  2, 850,  50, 600, -1, 5),
    OP(300,   4, 26,  1, 230,   0, 170,  3, 0),
    OP(100,  -6, 58,  2, 760,  45, 540, -1, 0),
    OP(500,   0, 16,  1, 150,   0, 120,  5, 0),
    OP(100,   8, 30,  2, 520,  20, 420, -1, 0)
  }, &BF_CP70 },
  { "DX7 E.PIANO", 6, {
    OP(100,   0, 70,  1,  45,   0, 120,  1, 3),   /* classic 1:1 bite  */
    OP(100,   0, 90,  2, 480,  28, 500, -1, 0),
    OP(100,  10, 62,  1,  60,   0, 140,  3, 0),
    OP(100, -10, 66,  2, 560,  34, 540, -1, 0),
    OP(1400,  0, 26,  1,  35,   0,  70,  3, 0),
    OP(300,   0, 20,  1, 260,   0, 220, -1, 0)
  }, &BF_DX7 },
  { "NORD STAGE", 6, {
    OP(100,   0, 46,  1, 130,   0, 170,  1, 0),
    OP(100,   0, 94,  2, 800,  56, 580, -1, 0),
    OP(300,   5, 28,  1, 190,   0, 160,  3, 0),
    OP(100,  -5, 64,  2, 740,  50, 560, -1, 0),
    OP(400,   0, 14,  1, 120,   0, 100,  5, 0),
    OP(100,   9, 30,  2, 480,  18, 400, -1, 0)
  }, &BF_NORD },
  { "SOUL / MOTOWN", 4, {
    OPW(100,   0, 60,  1, 100,   0, 180,  1, 40, 2),
    OPW(100,   0, 96,  3, 920,  66, 680, -1,  0, 0),
    OPW(200,  -5, 22,  1,  65,   0, 120,  3,  0, 0),
    OPW(300,   6, 55,  2, 720,  50, 560, -1,  0, 0)
  }, &BF_SOUL },
  { "NEO-SOUL", 6, {
    OP(100,   0, 62,  1, 120,   0, 190,  1, 0),
    OP(100,   0, 95,  3, 880,  60, 660, -1, 0),
    OP(200,   6, 34,  1, 160,   0, 170,  3, 0),
    OP(100,  -6, 60,  2, 750,  48, 560, -1, 0),
    OP(700,   0, 18,  1,  55,   0, 100,  3, 0),
    OP(100,   8, 28,  2, 500,  16, 400, -1, 0)
  }, &BF_NEO },
  { "HAMMOND KEYS", 6, {                     /* drawbar-style carriers */
    OP(100,  -6, 60,  4, 5000, 98, 700, -1, 0),  /* 1'    */
    OP(200,   4, 46,  4, 5000, 98, 700, -1, 0),  /* 2'    */
    OP(300,  -3, 30,  4, 5000, 98, 700, -1, 0),  /* 2 2/3'*/
    OP(400,   5, 24,  4, 5000, 98, 700, -1, 0),  /* 4'    */
    OP(600,  -7, 14,  4, 5000, 98, 700, -1, 0),
    OP(800,   7, 10,  4, 5000, 98, 700, -1, 0)
  }, &BF_HAMMOND },
  /* Demo-only voice: a clean 2-op tine. No bell stacks, so the top line of
   * the bossa snippet reads as a melody instead of a wash of partials. */
  { "DEMO TINE", 2, {
    OP(100, 0, 28, 2,  80,  0, 140, 1, 0),    /* soft 1:1 bite */
    OP(100, 0, 95, 2, 900, 62, 650, -1, 0)    /* singing tine carrier */
  }, &BF_DEMO },
  /* OPL3 (YMF262) voices: two operators, waveform ROM + modulator feedback.
   * This is the rounder, hollow "sound-card" FM sound rather than DX7 glass. */
  { "OPL3 E.PIANO", 2, {
    OPW(100, 0, 64, 1, 100, 0, 220, 1, 45, 1),   /* fallback if core unavailable */
    OPW(100, 0, 96, 2, 950, 58, 650, -1, 0, 0)
  }, &BF_OPL3_EPIANO },
  { "OPL3 BELL", 2, {
    OPW(300, 0, 56, 1, 90, 0, 190, 1, 35, 2),
    OPW(100, 0, 92, 2, 700, 24, 700, -1, 0, 0)
  }, &BF_OPL3_BELL },
  { "STAGE PIANO", 4, {
    OP(200,   0, 62, 1,  90,  0, 180, 1, 4),
    OP(100,   0, 94, 2, 850, 35, 900, -1, 0),
    OP(500,   0, 30, 1,  60,  0, 120, 3, 2),
    OP(100,   0, 70, 2, 700, 25, 750, -1, 0)
  }, &BF_PIANO },
  { "JAZZ ROOM", 4, {
    OP(100,  0, 98, 2, 1600, 70, 1500, -1, 0),  /* bar fundamental */
    OP(400,  0, 66, 2, 1200, 55, 1200, -1, 0),  /* 4th partial      */
    OP(300,  0, 46, 2,  900, 40,  900, -1, 0),  /* 3rd partial      */
    OP(1000, 0, 34, 2,  700, 30,  700, -1, 0)   /* upper shimmer    */
  }, &BF_VIBE },
  { "ELECTRIC ORGAN", 4, {
    OPW(100, 0, 58, 4, 5000, 98, 700, -1, 0, 0),
    OPW(200, 0, 44, 4, 5000, 98, 700, -1, 0, 1),
    OPW(400, 0, 30, 4, 5000, 98, 700, -1, 0, 3),
    OPW(600, 0, 22, 4, 5000, 98, 700, -1, 0, 3)
  }, &BF_EORGAN },
  { "VOX CONTINENTAL", 4, {
    OPW(100, 0, 56, 4, 5000, 98, 700, -1, 0, 0),
    OPW(200, 0, 44, 4, 5000, 98, 700, -1, 0, 2),
    OPW(300, 0, 32, 4, 5000, 98, 700, -1, 0, 1),
    OPW(500, 0, 24, 4, 5000, 98, 700, -1, 0, 3)
  }, &BF_VOX },
  { "FARFISA COMPACT", 4, {
    OPW(100, 0, 50, 4, 5000, 98, 700, -1, 0, 0),
    OPW(200, 0, 40, 4, 5000, 98, 700, -1, 0, 3),
    OPW(400, 0, 30, 4, 5000, 98, 700, -1, 0, 1),
    OPW(600, 0, 22, 4, 5000, 98, 700, -1, 0, 3)
  }, &BF_FARFISA },
  { "ACE TONE", 4, {
    OPW(100, 0, 54, 4, 5000, 98, 700, 1, 20, 1),
    OPW(100, 0, 90, 4, 5000, 98, 700, -1, 0, 0),
    OPW(200, 0, 64, 4, 5000, 98, 700, -1, 0, 0),
    OPW(300, 0, 52, 4, 5000, 98, 700, -1, 0, 2)
  }, &BF_ACETONE },
  { "CHEEZE ORG", 4, {
    OPW( 50, 0, 56, 4, 5000, 98, 700, 1, 18, 0),   /* low FM pair mod    */
    OPW(100, 0, 90, 4, 5000, 98, 700, -1, 0, 0),   /* low FM pair carrier*/
    OPW(100, 0, 50, 4, 5000, 98, 700, 3, 18, 3),   /* upper FM pair mod  */
    OPW(200, 0, 84, 4, 5000, 98, 700, -1, 0, 3)    /* upper FM pair car. */
  }, &BF_DMX_CHEEZE },
  { "60S ORGAN", 4, {
    OPW(400,    0, 56, 4, 5000, 98, 700, 1, 14, 0),  /* 4x FM body mod    */
    OPW(100,    0, 90, 4, 5000, 98, 700, -1, 0, 0),  /* FM body carrier   */
    OPW(300, 1200, 50, 4, 5000, 98, 700, -1, 0, 0),  /* +oct 3x drawbar   */
    OPW(100, 1200, 80, 4, 5000, 98, 700, -1, 0, 0)   /* +oct 1x drawbar   */
  }, &BF_DMX_60S },
  { "CHIP E.PIANO", 4, {
    OPW(100,  0, 64, 1, 100,  0, 220,  1, 50, 1),   /* half-sine bite    */
    OPW(100,  0, 96, 2, 950, 58, 650, -1,  0, 0),   /* sine body         */
    OPW(200,  3, 42, 1,  90,  0, 180,  3, 20, 2),   /* 2x edge           */
    OPW(100, -3, 70, 2, 700, 25, 700, -1,  0, 0)    /* upper sine        */
  }, &BF_CHIP_EP },

  /* Retained WOPL additions: Percussive Organ and FM5 Grand Piano. */
  { "PERCUSSIVE ORGAN", 2, {
    OPW( 50, 0, 58, 4, 5000, 98, 700, 1, 14, 1),
    OPW(400, 0, 92, 4, 5000, 98, 700, -1, 0, 0)
  }, &BF_PERC_ORG },
  { "FM5 GRAND PIANO", 4, {
    OPW(500, 0, 50, 1, 90, 12, 140, 1, 32, 0),
    OPW(100, 0, 94, 1, 800, 70, 700, -1, 0, 0),
    OPW(200, 2, 44, 1, 90, 12, 130, 1, 28, 0),
    OPW(100, 2, 88, 1, 780, 68, 680, -1, 0, 0)
  }, &BF_FM5_GRAND }
};
#undef OP

static float sine_tab[FM_SINE_N + 1];
static int g_patch;                       /* atomic patch request */
static float g_sr = 48000.0f;

typedef struct {
  int active;
  int note;
  int nops;
  unsigned long age;
  float phase[FM_OPS];
  float prev[FM_OPS];
  float env[FM_OPS];
  float sus[FM_OPS];
  float inc[FM_OPS];
  float coef_a[FM_OPS], coef_d[FM_OPS], coef_r[FM_OPS];
  float level[FM_OPS], fb[FM_OPS];
  int dest[FM_OPS], st[FM_OPS];          /* 0 off, 1 A, 2 D, 3 S, 4 R */
  int wave[FM_OPS];                     /* OPL3 waveform ROM selection */
  float sub_phase, sub_inc, sub_level;
  float pan_l, pan_r;
  float mod_scale;
} fm_voice;

static fm_voice voices[FM_VOICES];
static unsigned long age_counter;

static float coef_ms(float ms)
{
  float t;
  if (ms < 1.0f) ms = 1.0f;
  if (ms > 20000.0f) ms = 20000.0f;
  t = ms * 0.001f * g_sr;
  return 1.0f - vd_exp2(-1.442695f / t);
}

static inline float sin01(float phase)
{
  float x, f;
  int i;
  phase -= (int)phase;
  if (phase < 0.0f) phase += 1.0f;
  x = phase * (float)FM_SINE_N;
  i = (int)x;
  f = x - (float)i;
  return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}

/* The four OPL2/OPL3 waveform-ROM shapes, indexed by phase in turns:
 * 0 sine, 1 positive half only, 2 absolute sine, 3 quarter pulse. */
static inline float osc01(float phase, int wave)
{
  float s;
  if (wave <= 0) return sin01(phase);
  if (wave == 3) return phase < 0.25f ? sin01(phase * 4.0f) : 0.0f;
  s = sin01(phase);
  if (wave == 1) return phase < 0.5f ? s : 0.0f;
  return s < 0.0f ? -s : s;                 /* wave 2 */
}

/* ------------------------------------------------------------------ Nuked OPL3
 * A single YMF262 chip renders every current voice. The six-op engine remains
 * as a fallback for patches with no OPL3 recipe, but all BAR-FLY patches have
 * one. All of this runs in the audio flow only -- no locks, no host calls.
 */
static opl3_chip g_opl3;

typedef struct {
  int active;
  int note;
  int fnum, block;
  unsigned long age;
} opl3_voice;

static opl3_voice opl3_voices[18];
static int opl3_tail;   /* frames left to render after the last key off */

/* First slot of each chip channel (from the YMF262 register map). */
static const uint8_t BF_CH_SLOT[18] = {
  0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32
};

/* Convert a chip slot number into the register address used by Nuked. */
static int opl3_slot_reg(int slot, int base)
{
  int local = slot % 18, off;
  if (local < 6) off = local;
  else if (local < 12) off = local + 2;
  else off = local + 4;
  return base + off + (slot >= 18 ? 0x100 : 0);
}

/* Channel register: channels 9..17 live in the second register bank. */
static int opl3_chan_reg(int ch, int base)
{
  return ch < 9 ? base + ch : 0x100 + base + (ch - 9);
}

/* Convert a note to the chip's f-num/block pair. The OPL3 runs at 49716 Hz
 * internally (Nuked resamples to the host rate): f = fn * 2^(block-1) * fchip / 2^19. */
static void opl3_note_freq(int note, int *fnum, int *block)
{
  float hz = vd_mtof((float)note);
  int b = 4, fn = 0, i;
  for (i = 0; i < 12; i++) {
    float denom = 49716.0f * (float)(1 << (b - 1));
    fn = (int)(hz * 524288.0f / denom + 0.5f);
    if (fn > 1023 && b < 7) { b++; continue; }
    if (fn < 64 && b > 1) { b--; continue; }
    break;
  }
  if (fn < 0) fn = 0;
  if (fn > 1023) fn = 1023;
  *fnum = fn;
  *block = b;
}

static void opl3_note_on(int note, int vel, const opl3_patch_def *p)
{
  int chosen[2], need, pass, k, i, best;
  int ms, cs, fn, blk, detune, vtl, data, note2;
  float det, fine;
  const opl3_op_recipe *mod, *car;

  need = p->nch;
  if (need < 1) need = 1;
  if (need > 2) need = 2;

  /* Pick the channels: free ones first, then the oldest sounding ones. */
  for (pass = 0; pass < need; pass++) {
    best = -1;
    for (i = 0; i < 18; i++) {
      int taken = 0;
      for (k = 0; k < pass; k++) if (chosen[k] == i) taken = 1;
      if (taken) continue;
      if (!opl3_voices[i].active) { best = i; break; }
      if (best < 0 || opl3_voices[i].age < opl3_voices[best].age) best = i;
    }
    if (best < 0) best = 0;
    chosen[pass] = best;
  }

  detune = barfly_ctrl_atom(FM_C_DETUNE);
  det = vd_exp2((float)detune * 0.00028818585f);
  vtl = (127 - vel) * 24 / 127;   /* velocity -> total level */

  for (pass = 0; pass < need; pass++) {
    int ch = chosen[pass];
    mod = &p->op[pass][0];
    car = &p->op[pass][1];
    ms = BF_CH_SLOT[ch];
    cs = ms + 3;

    /* Key off while rewriting this channel's operators. */
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_chan_reg(ch, 0xb0), 0);

    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(ms, 0x20),
                  (uint8_t)(mod->avekm ? mod->avekm : (0x10 | (mod->mult & 0x0f))));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(cs, 0x20),
                  (uint8_t)(car->avekm ? car->avekm : (0x10 | (car->mult & 0x0f))));

    data = (int)mod->tl + vtl; if (data > 63) data = 63;
    data |= (int)mod->ksl << 6;
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(ms, 0x40), (uint8_t)data);
    data = (int)car->tl + vtl; if (data > 63) data = 63;
    data |= (int)car->ksl << 6;
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(cs, 0x40), (uint8_t)data);

    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(ms, 0x60),
                  (uint8_t)((mod->ar << 4) | mod->dr));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(cs, 0x60),
                  (uint8_t)((car->ar << 4) | car->dr));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(ms, 0x80),
                  (uint8_t)((mod->sl << 4) | mod->rr));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(cs, 0x80),
                  (uint8_t)((car->sl << 4) | car->rr));

    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(ms, 0xe0), (uint8_t)(mod->wave & 7));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_slot_reg(cs, 0xe0), (uint8_t)(car->wave & 7));

    /* Bank instruments can tune their two voices independently. */
    note2 = note + p->ko[pass];
    if (note2 < 0) note2 = 0;
    if (note2 > 127) note2 = 127;
    opl3_note_freq(note2, &fn, &blk);
    fine = vd_exp2((float)p->fine[pass] / 384.0f);   /* 1/32 semitone */
    fn = (int)((float)fn * det * fine + 0.5f);
    if (fn < 0) fn = 0;
    if (fn > 1023) fn = 1023;

    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_chan_reg(ch, 0xa0), (uint8_t)(fn & 0xff));
    data = (int)(0xf0 | ((p->fb[pass] & 7) << 1) | (p->con[pass] & 1));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_chan_reg(ch, 0xc0), (uint8_t)data);

    opl3_voices[ch].active = 1;
    opl3_voices[ch].note = note;
    opl3_voices[ch].fnum = fn;
    opl3_voices[ch].block = blk;
    opl3_voices[ch].age = ++age_counter;

    data = (int)(0x20 | ((fn >> 8) & 3) | ((blk & 7) << 2));
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_chan_reg(ch, 0xb0), (uint8_t)data);
  }
  opl3_tail = (int)g_sr * 2;   /* keep rendering through the release tail */
}

static void opl3_note_off(int note)
{
  int i, b0;
  for (i = 0; i < 18; i++) {
    if (!opl3_voices[i].active || opl3_voices[i].note != note) continue;
    b0 = ((opl3_voices[i].fnum >> 8) & 3) | ((opl3_voices[i].block & 7) << 2);
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_chan_reg(i, 0xb0), (uint8_t)b0);
    opl3_voices[i].active = 0;
    opl3_tail = (int)g_sr * 2;
  }
}

static void opl3_all_off(void)
{
  int i, b0;
  for (i = 0; i < 18; i++) {
    if (!opl3_voices[i].active) continue;
    b0 = ((opl3_voices[i].fnum >> 8) & 3) | ((opl3_voices[i].block & 7) << 2);
    OPL3_WriteReg(&g_opl3, (uint16_t)opl3_chan_reg(i, 0xb0), (uint8_t)b0);
    opl3_voices[i].active = 0;
    opl3_tail = (int)g_sr * 2;
  }
}

void fm_init(float sample_rate)
{
  int i;
  g_sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
  for (i = 0; i <= FM_SINE_N; i++)
    sine_tab[i] = vd_sin((float)i / (float)FM_SINE_N);
  OPL3_Reset(&g_opl3, (uint32_t)g_sr);
  OPL3_WriteReg(&g_opl3, 0x105, 0x01);   /* OPL3 mode: 8 waveforms + stereo */
  OPL3_WriteReg(&g_opl3, 0x104, 0x00);   /* all channels in 2-op mode */
  OPL3_WriteReg(&g_opl3, 0x08,  0x00);   /* normal note select */
  OPL3_WriteReg(&g_opl3, 0xbd,  0x00);   /* chip tremolo/vibrato/rhythm off */
  fm_reset();
}

void fm_reset(void)
{
  int i;
  for (i = 0; i < FM_VOICES; i++) voices[i].active = 0;
  opl3_all_off();
}

int fm_patch_count(void)
{
  return (int)(sizeof(PATCHES) / sizeof(PATCHES[0]));
}

const char *fm_patch_name(int index)
{
  if (index < 0 || index >= fm_patch_count()) index = 0;
  return PATCHES[index].name;
}

void fm_set_patch(int index)
{
  int n = fm_patch_count();
  if (index < 0) index = 0;
  if (index >= n) index = n - 1;
  __atomic_store_n(&g_patch, index, __ATOMIC_RELEASE);
}

int fm_voice_count(void)
{
  int i, n = 0;
  for (i = 0; i < FM_VOICES; i++) if (voices[i].active) n++;
  for (i = 0; i < 18; i++) if (opl3_voices[i].active) n++;
  return n;
}

/* Multiply a parameter given x1000 by a patch time, with a sane floor. */
static float scale_ms(int ms, int x1000)
{
  float v = (float)ms * (float)x1000 * 0.001f;
  if (v < 1.0f) v = 1.0f;
  if (v > 20000.0f) v = 20000.0f;
  return v;
}

static void note_on(int note, int vel, int pi)
{
  const fm_patch *p;
  fm_voice *v;
  int i, free = -1, oldest = 0;
  int index, decay, feedback, harmonic, detune, attack, release,
      velocity, sub;
  float hz, det, vel_amp, vel_mod, spread, pan;

  if (note < 0) note = 0;
  if (note > 127) note = 127;
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  if (pi < 0 || pi >= fm_patch_count()) pi = 0;

  for (i = 0; i < FM_VOICES; i++) {
    if (!voices[i].active) { free = i; break; }
    if (i == 0 || voices[i].age < voices[oldest].age) oldest = i;
  }
  if (free < 0) free = oldest;

  p = &PATCHES[pi];
  v = &voices[free];

  index    = barfly_ctrl_atom(FM_C_INDEX);
  decay    = barfly_ctrl_atom(FM_C_DECAY);
  feedback = barfly_ctrl_atom(FM_C_FEEDBACK);
  harmonic = barfly_ctrl_atom(FM_C_HARMONIC);
  detune   = barfly_ctrl_atom(FM_C_DETUNE);
  attack   = barfly_ctrl_atom(FM_C_ATTACK);
  release  = barfly_ctrl_atom(FM_C_RELEASE);
  velocity = barfly_ctrl_atom(FM_C_VELOCITY);
  sub      = barfly_ctrl_atom(FM_C_SUB);
  spread   = (float)barfly_ctrl_atom(FM_C_SPREAD) * 0.001f;

  if (index < 0) index = 0;
  if (decay < 0) decay = 0;
  if (attack < 0) attack = 0;
  if (release < 0) release = 0;
  if (harmonic < 0) harmonic = 0;
  if (feedback < 50) feedback = 0;

  hz = vd_mtof((float)note);
  det = vd_exp2((float)detune * 0.00028818585f);   /* cents -> ratio */

  vel_amp = 0.35f + 0.65f * (float)vel / 127.0f;
  vel_mod = vel_amp;
  if (velocity > 0) {
    float s = (float)velocity * 0.001f * 1.6f;
    float v01 = (float)vel / 127.0f;
    vel_mod *= 0.55f + 0.45f * vd_pow(v01 > 0.0f ? v01 : 0.01f, s);
  }

  v->active = 1;
  v->note = note;
  v->age = ++age_counter;
  v->nops = p->nops;
  if (v->nops > FM_OPS) v->nops = FM_OPS;
  v->mod_scale = (float)index * 0.001f;

  for (i = 0; i < FM_OPS; i++) {
    const fm_op_def *d = &p->op[i];
    float ratio = (float)d->ratio * 0.01f;
    float level = (float)d->level * 0.01f;
    int dest = d->dest;

    v->wave[i] = d->wave;
    if (i >= v->nops || level <= 0.0f) {
      v->level[i] = 0.0f;
      v->inc[i] = 0.0f;
      v->st[i] = 0;
      v->env[i] = 0.0f;
      v->dest[i] = -2;
      v->fb[i] = 0.0f;
      continue;
    }
    if (ratio >= 400.0f) level *= (float)harmonic * 0.001f;
    v->level[i] = level * (dest < 0 ? vel_amp : vel_mod);
    v->dest[i] = dest;
    v->phase[i] = 0.0f;
    v->prev[i] = 0.0f;
    v->inc[i] = hz / g_sr * ratio * det *
                vd_exp2((float)d->detune * 0.00028818585f);
    v->fb[i] = (float)d->fb * 0.01f + (float)feedback * 0.0001f;
    if (v->fb[i] > 0.70f) v->fb[i] = 0.70f;
    v->sus[i] = (float)d->sl * 0.01f;
    if (dest >= 0) v->sus[i] *= 0.35f;   /* index dies away faster */
    v->coef_a[i] = coef_ms(scale_ms(d->ar, attack));
    v->coef_d[i] = coef_ms(scale_ms(d->dr, decay));
    v->coef_r[i] = coef_ms(scale_ms(d->rr, release));
    v->env[i] = 0.0f;
    v->st[i] = 1;
  }

  v->sub_inc = hz / g_sr;
  v->sub_phase = 0.0f;
  v->sub_level = (float)sub * 0.001f * vel_amp * 0.35f;

  /* Alternate voices across the stereo field; spread widens it. */
  pan = ((free & 1) ? 1.0f : -1.0f) * (0.18f + 0.32f * spread);
  pan += ((note % 12) - 5.5f) * 0.012f * spread;
  if (pan > 0.95f) pan = 0.95f;
  if (pan < -0.95f) pan = -0.95f;
  v->pan_l = 0.7071f * vd_sqrt(1.0f - pan);
  v->pan_r = 0.7071f * vd_sqrt(1.0f + pan);
}

void fm_note_on(int note, int vel)
{
  int pi = __atomic_load_n(&g_patch, __ATOMIC_ACQUIRE);
  if (PATCHES[pi].opl3) opl3_note_on(note, vel, PATCHES[pi].opl3);
  else note_on(note, vel, pi);
}

/* Demo notes use a fixed clean voice; the player's selected patch is untouched. */
void fm_note_on_patch(int note, int vel, int patch)
{
  if (patch < 0 || patch >= fm_patch_count()) patch = 0;
  if (PATCHES[patch].opl3) opl3_note_on(note, vel, PATCHES[patch].opl3);
  else note_on(note, vel, patch);
}

void fm_note_off(int note)
{
  int i;
  opl3_note_off(note);
  for (i = 0; i < FM_VOICES; i++) {
    if (voices[i].active && voices[i].note == note) {
      int j;
      for (j = 0; j < voices[i].nops; j++) {
        if (voices[i].level[j] > 0.0f) voices[i].st[j] = 4;
      }
      voices[i].sub_level = 0.0f;
    }
  }
}

void fm_all_off(void)
{
  int i, j;
  opl3_all_off();
  for (i = 0; i < FM_VOICES; i++) {
    if (!voices[i].active) continue;
    for (j = 0; j < voices[i].nops; j++) {
      if (voices[i].level[j] > 0.0f) voices[i].st[j] = 4;
    }
    voices[i].sub_level = 0.0f;
  }
}

void fm_render(float *l, float *r, int n, const fm_ctrl *c)
{
  float vib[VIBE_MAX_BLOCK];
  float vib_phase = 0.0f, vib_step;
  int i, v, o, s;

  vib_step = (float)c->vib_rate * 0.001f / g_sr;
  if (vib_step < 0.0f) vib_step = 0.0f;
  if (vib_step > 0.1f) vib_step = 0.1f;
  for (i = 0; i < n; i++) {
    vib[i] = 1.0f + (float)c->vib_depth * 0.0004f * vd_sin(vib_phase);
    vib_phase += vib_step;
    if (vib_phase >= 1.0f) vib_phase -= 1.0f;
  }

  for (i = 0; i < n; i++) { l[i] = 0.0f; r[i] = 0.0f; }

  /* Nuked YMF262: one resampled stereo frame per output frame. Only run the
   * chip while a voice is sounding or its release tail is ringing. */
  if (opl3_tail > 0) {
    for (i = 0; i < n; i++) {
      int16_t smp[2];
      OPL3_GenerateResampled(&g_opl3, smp);
      l[i] += (float)smp[0] * (1.0f / 32768.0f) * 2.0f;
      r[i] += (float)smp[1] * (1.0f / 32768.0f) * 2.0f;
    }
    opl3_tail -= n;
    if (opl3_tail < 0) opl3_tail = 0;
  }

  for (v = 0; v < FM_VOICES; v++) {
    fm_voice *vo = &voices[v];
    if (!vo->active) continue;
    for (s = 0; s < n; s++) {
      float mod[FM_OPS];
      float sl = 0.0f, sr = 0.0f;
      float vibes = vib[s];
      int live = 0;

      for (o = 0; o < FM_OPS; o++) mod[o] = 0.0f;

      for (o = 0; o < vo->nops; o++) {
        float out, ph, e;
        if (vo->level[o] <= 0.0f) continue;
        e = vo->env[o];
        if (vo->st[o] == 1) {
          e += (1.0f - e) * vo->coef_a[o];
          if (e > 0.998f) { e = 1.0f; vo->st[o] = 2; }
        } else if (vo->st[o] == 2) {
          float target = vo->sus[o];
          e += (target - e) * vo->coef_d[o];
          if (vd_absf(e - target) < 0.008f) { e = target; vo->st[o] = 3; }
        } else if (vo->st[o] == 4) {
          e += (0.0f - e) * vo->coef_r[o];
          if (e < 0.0015f) { e = 0.0f; vo->st[o] = 0; }
        }
        vo->env[o] = e;
        if (e <= 0.0f) continue;
        live = 1;

        ph = vo->phase[o] + vo->inc[o] * vibes + mod[o] +
             vo->prev[o] * vo->fb[o];
        ph -= (int)ph;
        if (ph < 0.0f) ph += 1.0f;
        vo->phase[o] = ph;
        out = osc01(ph, vo->wave[o]) * e * vo->level[o];
        vo->prev[o] = out;

        if (vo->dest[o] >= 0) mod[vo->dest[o]] += out * vo->mod_scale;
        else if (vo->pan_l >= vo->pan_r) sl += out * 2.0f * vo->pan_l;
        else sr += out * 2.0f * vo->pan_r;
      }

      if (vo->sub_level > 0.0f) {
        float ph = vo->sub_phase;
        ph += vo->sub_inc * vibes;
        ph -= (int)ph;
        vo->sub_phase = ph;
        {
          float x = sin01(ph) * vo->sub_level * 2.0f;
          if (vo->pan_l >= vo->pan_r) sl += x * vo->pan_l;
          else sr += x * vo->pan_r;
        }
        live = 1;
      }
      l[s] += sl;
      r[s] += sr;
      if (!live && vo->sub_level <= 0.0f) { vo->active = 0; break; }
    }
  }
}
