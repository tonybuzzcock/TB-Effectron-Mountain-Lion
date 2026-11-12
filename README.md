# TB Effectron (Mountain Lion VST)

A model of the **DeltaLab Effectron I, ADM 1020** adaptive delta modulation digital delay, as a
**VST 2.4, 64-bit Intel plugin for Mac OS X 10.8.5** (Pro Tools 11 through Blue Cat PatchWork).
Non-commercial, for personal use.

There is **no schematic**. The sound is a behavioural model built from DeltaLab's published
specifications (15 kHz bandwidth at all settings, 87 dB dynamic range, over 1 second of delay, 8:1
flange sweep) and the front panel of the user's unit (`Docs/Effectron_I_ADM1020_front_panel.png`).

The modern Mac version (CLAP / VST3 / AU) will live in `TB-Effectron-2026-MAC`.

## Controls

Front panel, as on the unit, left to right:

| Section | Control | Range |
|---|---|---|
| INPUT | LEVEL | 0 to MAX (x8, audio taper); unity through the unit at 5.75 |
| INPUT | LIMIT LED | lights above 2 V peak at the converter |
| | FEEDBACK | NEG (inverted repeats) .. 0 at 12 o'clock .. POS |
| DIGITAL DELAY (ms) | two red push buttons | both out FLANGE 4-16, right in DOUBLE 16-64, left in SH ECHO 64-256, both in LG ECHO 256-1024 |
| MODULATOR | DELAY (outer black knob) | MAX fully left, MIN at 12 o'clock; past 12 the envelope follower sweeps the delay with your playing, more the further it is turned |
| MODULATOR | WIDTH (inner teal cap, 12 to 5 o'clock) | LFO sweep 0 to 100 % (8:1) |
| MODULATOR | SPEED | 0.05 to 12 Hz |
| OUTPUT | MIX | SOURCE .. EQUAL .. EFFECT |

**REPEAT** (infinite repeat) is a third red push button on the left, in place of the DeltaLab badge.
On the unit it comes from a footswitch on the back. With it in, the memory recirculates for ever, and
DELAY changes the loop's pitch (MIN to halfway is an octave).

Every push button lights up red when it is in. The TB electronic logo is at the right-hand end. The
panel keeps the unit's colours: the blue face, white lettering, black knurled knobs with teal caps
(SPEED all black), and the red buttons.

Plugin strip: Input (+-12 dB), Output (+-24 dB), 8 presets, readout.

## How it's modelled

- **Input**: LEVEL, a 15 kHz 3rd-order low-pass, the converter's input linear to 1.8 V and rounding
  into 2.6 V, plus a little analogue hiss (the spec's 87 dB).
- **Delta modulator**: one bit per clock tick, an integrator with a slight leak, and a syllabic
  step-size filter (the step decays, and is pushed up whenever three bits in a row agree), as in
  CVSD / ADM hardware. The demodulator is the same circuit fed from memory, so its noise, grain and
  slope overload come from the process itself.
- **Memory and clock**: each range has a fixed number of bits, and **the delay is set by the clock**
  (delay = bits / clock). Sweeping DELAY or the LFO bends the pitch of what is already in memory,
  and REPEAT recirculates the bits, as on the unit.
- **Output**: a 15 kHz reconstruction low-pass. FEEDBACK goes back to the converter's input; MIX
  crossfades the input and the effect.
- Range buttons fade the effect out and back in over 10 ms while the memory restarts (the real
  unit just glitches).
- Plain C++98, no allocation while processing, no latency.

### Estimates (not fixed by any published figure)

All in `Source/DSP/Effectron.cpp` as named constants. **Tune these against your unit:**

- `kFlangeBits` = 16384: the memory per range, so the clock runs 1.024 to 4.096 MHz across every
  range (1 Mbit for LG ECHO).
- `kStepMinV`, `kSlopeMaxVps`, `kStepTauS`: the delta modulator's grain and slope limit. They give
  about -64 dB noise and distortion at 1 kHz at the bottom of a range and -54 dB at the top.
- `kHissV`: idle noise about -88 dB.
- `kLevelMax` = x8 with an audio taper; `kNominalV` = 2 V at the default LEVEL.
- `kKneeV`, `kClipV`, `kLimitLedV`: the converter's limit and the LIMIT LED threshold.
- `kFeedbackMax` = 1.0: at full POS or NEG the repeats go on for a long time without running away.
- `kWidthOct` = 1.5 octaves either way (8:1 at 100 %), triangle LFO.
- `kEnvOct`, `kEnvFullV`, attack 2 ms, release 150 ms: the envelope follower.
- `kSpeedMinHz` = 0.05, `kSpeedSpan` = 240: SPEED 0.05 to 12 Hz.
- The concentric knob: WIDTH's travel taken as 12 to 5 o'clock (its scale's 0 % and 100 %).

## Build

GitHub Actions builds the bundle on an Intel macOS runner with `-mmacosx-version-min=10.8.5`, checks
that the binary declares 10.8.5 and links only libraries that 10.8.5 has, loads it in a test host,
drives the panel offscreen and publishes screenshots to the `gui-preview` branch.

Local tests (Linux or Mac): `make -C Tests run vst-run`, sanitizers: `make -C Tests asan`.

## Install

Actions -> latest run -> Artifacts -> `TB-Effectron-mountain-lion-vst`. Unzip it (twice), copy
`TB Effectron.vst` to `/Library/Audio/Plug-Ins/VST/`, then rescan in PatchWork.
