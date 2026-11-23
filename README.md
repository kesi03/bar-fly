# BAR-FLY

![alt text](https://github.com/kesi03/bar-fly/raw/main/firmware/bar-fly/icon.png "Bar Fly"){ width: 200px; }


A lounge electric piano for [VIBE FM](https://github.com/Vibe-FM-SDK/vibe-fm-sdk): all 24 voices rendered by the real Nuked OPL3 (YMF262) core through a classic electric-piano FX rack, with a bar-fly motif on the device, in the editor and on the menu tile.

It plays immediately — keys and pads make sound from startup, and a short bossa-nova snippet (an Ipanema-style turnaround with an original top line) runs through the same voices and effects.

## Sound

- **Real Nuked OPL3 engine**: YMF262 v1.8 waveform ROM, envelopes and feedback; 24 register recipes. Organ voices use two OPL3 channels per note for four additive drawbars.
- **Bank-derived classic voices**: the Wurlitzer layers two real DMXOPL3/OPLSynth bank records; `CHEEZE ORG`, `60S ORGAN`, `PERCUSSIVE ORGAN` and `FM5 GRAND PIANO` are direct bank transcriptions. The Rhodes, Hammond and Stage Piano started from auditioned bank records and were re-voiced with Nuked OPL3 envelope/register calibration. `JAZZ ROOM` is the only vibraphone; `OPL3 E.PIANO`, `NORD STAGE` and `SOUL / MOTOWN` are original OPL3 register designs. The unconvincing harpsichord is gone.
- **Twenty factory presets**: `MK I RHODES`, `MK II RHODES`, `WURLI 200A`, `CP-70`, `OPL3 E.PIANO`, `NORD STAGE`, `SOUL / MOTOWN`, `NEO-SOUL`, `JAZZ ROOM`, `HAMMOND KEYS`, `OPL3 BELL`, `STAGE PIANO`, `ELECTRIC ORGAN`, `VOX CONTINENTAL`, `FARFISA COMPACT`, `ACE TONE`, `CHEEZE ORG`, `60S ORGAN`, `PERCUSSIVE ORGAN`, `FM5 GRAND PIANO`. The recipe set also includes `DEMO TINE` for the bossa snippet. The retained WOPL voices are presets 18–19. Presets 4–6 use three distinct electric-piano recipes. The old isolation-test preset is gone; every factory preset is a playable voice.
- **Classic EP FX rack**, in order:

  `tube/solid saturation → compressor → 3-band EQ → auto-wah → ring modulation → phaser → stereo chorus → flanger → tremolo / auto-pan → Leslie → ping-pong / tape delay → room / hall / plate / spring reverb → bitcrusher → tone → limiter → master`
- **Colour rack additions**: flanger (short feedback comb), auto-wah (envelope-following bandpass), ring modulation (sine multiplier) and bitcrusher (sample-and-hold plus quantisation) are clean-room DSP, default off and mixed in from the `COLOR` page.

- **Leslie rotary speaker**, rebuilt as a clean-room dual-rotor model: 800 Hz crossover, independent horn/drum motors with realistic spin-up and brake, geometry-based Doppler and inverse-distance gain, three baffle taps per rotor, horn/drum voicing and a stereo mic pair. Slow mode is tuned slightly quicker so the sweep does not hang and read as a fade. It is off by default in every factory preset and can be toggled with F6; the Doppler reads as pitch wobble on pure drawbars.
- **Preset recipes**: Soul/Motown = comp → auto-wah → phaser → trem → plate (the only factory preset with the phaser on); Jazz = vibraphone → gentle chorus/room tremolo; Neo-Soul = tube saturation → stereo chorus → tape delay → plate; Percussive Organ = direct bank FM/additive organ → chorus/trem → room verb; FM5 Grand = direct 5x/2x FM stacks → tape delay → hall. The new colour effects are used sparingly: flanger on MK II Rhodes, bitcrush on OPL3 E.PIANO and ring modulation on Ace Tone.

## Controls

The base VIBE layout is used throughout. Every value on the display is reachable with **DATA + ENTER** (FOCUS); `HOME` tapped shows the page overview.

| Control | Action |
| --- | --- |
| Keys / MIDI notes | Play the electric piano |
| Pads 0–7 / 8–15 | Bossa four-note voicings / single stabs |
| Encoder A / F1 | Next display page |
| Encoder B / F2 | Next factory voice |
| Knobs 1–4 | Edit the four values shown above F1–F6 |
| F3 | Audition chord |
| F4 | Next factory voice |
| F5 / HOME | Controls overview |
| F6 | Leslie on / off |
| K1–K6 | SOUND · ENV · LFO · FX · SPACE · DEMO pages |
| REC | Panic / all notes off |
| STOP / PLAY | Bossa snippet off / on |
| RETURN | Back to SOUND |

Pages are `SOUND`, `ENV`, `FM`, `TONE`, `FX`, `LFO / MOD`, `SPACE`, `COLOR`, `SYSTEM`. The hexagon K-keys carry the shortcuts; the display shows the page names and the values.

## Build

Requirements: Zig 0.13.0, Node.js, Python 3, `sh`.

```sh
# If the SDK is missing, fetch the pinned commit:
sh scripts/fetch-sdk.sh

# Build:
cd sdk
ZIG=zig sh tools/build.sh ../firmware/bar-fly
# -> build/bar-fly.wasm, build/bar-fly.zip, build/bar-fly.editor/, build/bar-fly.png
```

On Windows, `python3` can be a Microsoft Store alias. `scripts/build.ps1` sets up a local `python3.exe` shim and calls the same build script:

```powershell
pwsh -File scripts/build.ps1
```

Drop `sdk/build/bar-fly.zip` onto the VIBE FM VST window, or load the firmware in the browser emulator.

## Validate

```sh
node sdk/tools/vibe-check.mjs sdk/build/bar-fly.wasm \
  --manifest firmware/bar-fly/firmware.json --seconds 5

mkdir -p sdk/build/bar-fly-test
node sdk/tools/wasmtest.mjs sdk/build/bar-fly.wasm \
  firmware/bar-fly/wasmtest.json sdk/build/bar-fly-test
```

`vibe-check` verifies format, exports, parallel audio, FOCUS, HOME, storage, parameter/editor metadata, NaN safety and real-time limits. `wasmtest` renders audio and display snapshots for the six scenes in `firmware/bar-fly/wasmtest.json`, including the bossa snippet.

To regenerate the menu icon:

```sh
python tools/make_icon.py       # writes firmware/bar-fly/icon.png (960 x 540)
```

## Layout

```text
firmware/bar-fly/
  firmware.json       VIBE FM manifest (id: bar-fly, protocol 7)
  main.c              parameters, presets, events, audio flow, display, editor bridge
  fm.c / fm.h         Nuked OPL3 recipes, allocator and factory patches
  opl3.c / opl3.h     vendored Nuked OPL3 v1.8 core (LGPL-2.1-or-later)
  LICENSE-Nuked-OPL3.txt / NOTICE.txt  required attribution
  fx.c / fx.h         EP FX rack, including the clean dual-rotor Leslie
  editor/index.html   optional custom Editor page (window.vibe only)
  icon.png            960 x 540 bar-fly tile
  wasmtest.json       scripted audio/display test
tools/
  make_icon.py        stdlib-only icon generator
  build.ps1           Windows build wrapper
scripts/
  fetch-sdk.sh        pins the VIBE FM SDK commit
sdk/                  VIBE FM SDK (Apache-2.0), pinned at 2ac234088ee5e2da4503f380f3a167d253cc75d4
```

The audio flow owns the OPL3/FM and FX state; the control flow publishes parameters as atomic ints and hands notes over a small SPSC queue. The custom Editor page is a second face only — the firmware remains fully playable without it, on STANDARD cards and on the hardware.

## Credits

- Sound and code built with the VIBE FM SDK (Apache-2.0), pinned to the commit above.
- Leslie rotary speaker implemented clean-room for BAR-FLY: independent horn/drum motors, geometric Doppler, baffle taps and stereo mic mix (no third-party Leslie code or impulse responses).
- Flanger, auto-wah, ring modulator and bitcrusher are clean-room DSP written for BAR-FLY; no third-party effect code or impulse responses are used.
- Nuked OPL3 v1.8 by Nuke YKT, LGPL-2.1-or-later; vendored and lightly adapted for the VIBE FM no-libc runtime.
- DX/Dexed-style patch data inspiration from `fm1-emu/crates/fm1-soc/src/fwsc.rs` (`EMU E.PNO`, algorithm 5).
- FX patterns from `sound-font-player/firmware/sf2-player/fx.c`.
- Editor and icon patterns from the `wah-wah` project.

"VIBE FM" is a trademark of its owners; BAR-FLY is an independent firmware built with the VIBE FM SDK.
