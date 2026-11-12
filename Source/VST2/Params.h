// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls first, in the Effectron I's left-to-right order
// (INPUT LEVEL, FEEDBACK, the DIGITAL DELAY buttons as one four-way range,
// the MODULATOR's DELAY / WIDTH / SPEED, OUTPUT MIX), then REPEAT (the infinite
// repeat the unit takes from a rear footswitch, here on the face), then the
// plugin-only controls in the editor's bottom strip.

#ifndef TBEFFECTRON_PARAMS_H
#define TBEFFECTRON_PARAMS_H

namespace tbeffectron
{

enum Param
{
    // Front panel
    kLevel = 0, kFeedback, kRange, kDelay, kWidth, kSpeed, kMix, kRepeat,
    // Plugin only (bottom strip of the editor)
    kInput, kOutput,
    kNumParams
};

enum Kind { kKnob, kBipolar, kRangeSel, kDelayMs, kPercent, kSpeedHz, kMixPct, kSwitch, kDb12, kDb24 };

const char* const kParamNames[kNumParams] = {
    "Level", "Feedback", "Range", "Delay", "Width", "Speed", "Mix", "Repeat",
    "Input", "Output"
};

const int kParamKind[kNumParams] = {
    kKnob, kBipolar, kRangeSel, kDelayMs, kPercent, kSpeedHz, kMixPct, kSwitch,
    kDb12, kDb24
};

const float kDefaults[kNumParams] = {
    0.575f,     // LEVEL: 2 V at the converter for a 1.0 (0 dBFS) peak, unity through the unit
    0.5f,       // FEEDBACK at 12 o'clock: none
    2.0f / 3.0f,// SH ECHO (64-256 ms)
    0.25f,      // DELAY halfway from MAX to MIN: 128 ms on SH ECHO
    0.0f,       // WIDTH 0 %
    0.4f,       // SPEED 0.45 Hz
    0.5f,       // MIX at EQUAL
    0.0f,       // REPEAT off
    0.5f, 0.5f
};

const double kInputRangeDb  = 12.0;
const double kOutputRangeDb = 24.0;

// The four ranges (FLANGE, DOUBLE, SH ECHO, LG ECHO) as one parameter: 0, 1/3, 2/3, 1
inline int rangeIndex (float v)
{
    const int r = (int) (v * 3.0f + 0.5f);
    return r < 0 ? 0 : (r > 3 ? 3 : r);
}
inline float rangeValue (int r) { return (float) r / 3.0f; }
// The two buttons: left is in for SH ECHO and LG ECHO, right for DOUBLE and LG ECHO
inline bool leftButtonIn (int r)  { return r >= 2; }
inline bool rightButtonIn (int r) { return (r & 1) != 0; }
inline int  rangeFromButtons (bool left, bool right) { return (left ? 2 : 0) + (right ? 1 : 0); }

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbeffectron

#endif
