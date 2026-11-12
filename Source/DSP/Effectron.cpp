// Effectron.cpp
// See Effectron.h for the signal path. Every value below that DeltaLab's
// published specifications don't fix is a named estimate (kXxx), listed in
// the README: tune them against the real unit.

#include "Effectron.h"

#include <cmath>
#include <cstring>

namespace tbeffectron
{

namespace
{

// --- From the published specifications and the panel --------------------
const double kRangeMs[kNumRanges] = { 4.0, 16.0, 64.0, 256.0 };   // panel legend, 4:1 per range
const double kBandwidthHz = 15000.0;    // "15 kHz bandwidth, all settings" (Effectron I)

// --- Estimates ------------------------------------------------------------
// Memory per range: chosen so the clock runs 1.024 .. 4.096 MHz across
// every range (bits = 4.096 MHz x the range's minimum delay; 1 Mbit for
// LG ECHO, the size of the Effectrons' 1-second memories).
const int    kFlangeBits   = 16384;
const double kClockMinHz   = 250000.0;    // clock limits for the LFO / envelope sweeps
const double kClockMaxHz   = 8400000.0;
// Delta modulator
const double kStepMinV     = 0.00005;     // smallest step: the idle grain
const double kSlopeMaxVps  = 300000.0;    // largest step x clock: the steepest slope it can follow
const double kStepTauS     = 0.005;       // syllabic filter: the step's time constant. Leaky, as in
                                          // the hardware, so a recirculating loop settles, never drifts
const double kLeakTauS     = 0.002;       // integrator leak (encoder and decoder alike)
// Input stage
const double kLevelMax     = 8.0;         // LEVEL gain at MAX
const double kLevelTaper   = 2.5;         // audio taper
const double kNominalV     = 2.0;         // converter level at the default LEVEL: output scaled back by this
const double kKneeV        = 1.8;         // converter input: linear up to here,
const double kClipV        = 2.6;         // then rounding off into this limit
const double kHissV        = 0.00011;     // analogue noise at the converter (rms): the spec's 87 dB
const double kLimitLedV    = 2.0;         // LIMIT LED threshold (peak volts at the converter)
// Feedback, modulation
const double kFeedbackMax  = 1.0;         // |feedback| at NEG / POS fully turned
const double kWidthOct     = 1.5;         // LFO sweep at 100 %: +-1.5 octaves (8:1, the Effectron's flange ratio)
const double kEnvOct       = 2.0;         // envelope follower sweep at full: the range's 4:1
const double kEnvFullV     = 1.0;         // envelope level for the full sweep (volts at the converter)
const double kEnvAttackS   = 0.002;
const double kEnvReleaseS  = 0.15;
const double kSpeedMinHz   = 0.05;        // SPEED: 0.05 .. 12 Hz, exponential
const double kSpeedSpan    = 240.0;
const double kGlideS       = 0.05;        // DELAY knob glide (the pot's own smoothing)

const double kLn2 = 0.69314718055994530942;

// Bilinear designs at the host rate need prewarping (the template filters
// assume 4x); clamp below 0.45 fs so they stay stable at any rate.
double prewarp (double f, double fs)
{
    if (f > 0.45 * fs) f = 0.45 * fs;
    return fs / kPi * std::tan (kPi * f / fs);
}

inline double softLimit (double v)
{
    const double a = std::fabs (v);
    if (a <= kKneeV) return v;
    const double c = kKneeV + (kClipV - kKneeV) * std::tanh ((a - kKneeV) / (kClipV - kKneeV));
    return v < 0.0 ? -c : c;
}

inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

//==============================================================================
Effectron::Effectron()
    : fs (44100.0),
      levelT (0.575), level (0.0), fbT (0.0), fb (0.0), mixT (0.5), mix (0.5),
      inT (1.0), inG (1.0), outT (1.0), outG (1.0),
      delayPot (0.25), widthT (0.0), width (0.0), lfoInc (0.0), logDs (0.0),
      smK (0.0), glideK (0.0),
      range (kShortEcho), pendingRange (kShortEcho), bits (memoryBits (kShortEcho)), speedPot (0.4), repeat (false), wetFade (1.0),
      xPrev (0.0), yPrev (0.0), env (0.0), envAtt (0.0), envRel (0.0),
      limitPeak (0.0), limitRel (0.0),
      encEst (0.0), encStep (kStepMinV), decEst (0.0), decStep (kStepMinV),
      encHist (0), decHist (0), tickAcc (0.0), pos (0), lfoPhase (0.0),
      lastDelayMs (0.0), lastClock (0.0), noiseSeed (12345u)
{
    level = levelGain (levelT);
    setSpeed (0.4);
    prepare (44100.0);
}

void Effectron::prepare (double sampleRate)
{
    fs = sampleRate > 1000.0 ? sampleRate : 44100.0;
    smK = 1.0 - std::exp (-1.0 / (0.02 * fs));
    glideK = 1.0 - std::exp (-1.0 / (kGlideS * fs));
    envAtt = 1.0 - std::exp (-1.0 / (kEnvAttackS * fs));
    envRel = 1.0 - std::exp (-1.0 / (kEnvReleaseS * fs));
    limitRel = std::exp (-1.0 / (0.1 * fs));

    // 3rd-order Butterworth low-passes at the stated bandwidth
    const double fc = prewarp (kBandwidthHz, fs);
    aaA.lowpass (fc, 1.0, fs);  aaB.lowpass (fc, fs);
    recA.lowpass (fc, 1.0, fs); recB.lowpass (fc, fs);
    outHp.highpass (8.0, fs);
    lfoInc = lfoHz (speedPot) / fs;
    reset();
}

void Effectron::reset()
{
    aaA.reset(); aaB.reset(); recA.reset(); recB.reset(); outHp.reset();
    xPrev = yPrev = env = limitPeak = 0.0;
    encEst = decEst = 0.0;
    encStep = decStep = kStepMinV;
    encHist = decHist = 0;
    tickAcc = 0.0;
    lfoPhase = 0.0;
    range = pendingRange;
    bits = memoryBits (range);
    fillIdle();
    wetFade = 1.0;
    level = levelGain (levelT); fb = fbT; mix = mixT; inG = inT; outG = outT; width = widthT;
    logDs = std::log (manualDelayMs (range, delayPot < 0.5 ? delayPot : 0.5) * 0.001);
    lastDelayMs = std::exp (logDs) * 1000.0;
    lastClock = bits / std::exp (logDs);
}

// The idle pattern: alternating bits, what a delta modulator sends for silence
void Effectron::fillIdle()
{
    for (int w = 0; w < kMemWords; ++w) mem[w] = 0x55555555u;
    pos = 0;
}

//==============================================================================
// Mappings

double Effectron::rangeMinMs (int r)
{
    if (r < 0) r = 0;
    if (r >= kNumRanges) r = kNumRanges - 1;
    return kRangeMs[r];
}

int Effectron::memoryBits (int r)
{
    if (r < 0) r = 0;
    if (r >= kNumRanges) r = kNumRanges - 1;
    return kFlangeBits << (2 * r);
}

// MAX (4 x the range's minimum) fully left, MIN at 12 o'clock, exponential
double Effectron::manualDelayMs (int r, double v)
{
    v = clampd (v, 0.0, 0.5);
    return rangeMinMs (r) * std::pow (4.0, (0.5 - v) / 0.5);
}

double Effectron::lfoHz (double v)
{
    return kSpeedMinHz * std::pow (kSpeedSpan, clampd (v, 0.0, 1.0));
}

double Effectron::levelGain (double v)
{
    return kLevelMax * std::pow (clampd (v, 0.0, 1.0), kLevelTaper);
}

double Effectron::feedbackGain (double v)
{
    return (2.0 * clampd (v, 0.0, 1.0) - 1.0) * kFeedbackMax;
}

double Effectron::limitThreshold() { return kLimitLedV; }

//==============================================================================
// Setters (targets; smoothed in process)

void Effectron::setLevel (double v)    { levelT = clampd (v, 0.0, 1.0); }
void Effectron::setFeedback (double v) { fbT = feedbackGain (v); }
void Effectron::setRange (int r)       { pendingRange = r < 0 ? 0 : (r >= kNumRanges ? kNumRanges - 1 : r); }
void Effectron::setDelay (double v)    { delayPot = clampd (v, 0.0, 1.0); }
void Effectron::setWidth (double v)    { widthT = clampd (v, 0.0, 1.0); }
void Effectron::setSpeed (double v)    { speedPot = clampd (v, 0.0, 1.0); lfoInc = lfoHz (speedPot) / fs; }
void Effectron::setMix (double v)      { mixT = clampd (v, 0.0, 1.0); }
void Effectron::setRepeat (bool on)    { repeat = on; }
void Effectron::setInputGainDb (double dB)  { inT = std::pow (10.0, dB / 20.0); }
void Effectron::setOutputGainDb (double dB) { outT = std::pow (10.0, dB / 20.0); }

//==============================================================================
void Effectron::process (const float* in, float* out, int numSamples)
{
    const double fadeStep = 1.0 / (0.01 * fs);
    const double levelTarget = levelGain (levelT);
    const bool   rep = repeat;
    double lnMin = std::log (rangeMinMs (range) * 0.001);

    for (int i = 0; i < numSamples; ++i)
    {
        // A range button changes the memory: fade the effect out (10 ms),
        // start the new memory from the idle pattern, fade back in. The real
        // unit simply glitches; this keeps the buttons click-free.
        if (pendingRange != range && wetFade <= 0.0)
        {
            range = pendingRange;
            bits = memoryBits (range);
            fillIdle();
            decEst = 0.0; decStep = kStepMinV; decHist = 0;
            recA.reset(); recB.reset();
            yPrev = 0.0;
            lnMin = std::log (rangeMinMs (range) * 0.001);
            logDs = lnMin + (delayPot < 0.5 ? std::log (4.0) * (0.5 - delayPot) / 0.5 : 0.0);
        }
        // Pots
        level += (levelTarget - level) * smK;
        fb    += (fbT - fb) * smK;
        mix   += (mixT - mix) * smK;
        inG   += (inT - inG) * smK;
        outG  += (outT - outG) * smK;
        width += (widthT - width) * smK;

        // Input stage: LEVEL, then the anti-alias filter
        const double x = (double) in[i] * inG * level;
        const double xa = aaB.tick (aaA.tick (x));

        // Envelope follower
        const double ax = std::fabs (xa);
        env += (ax - env) * (ax > env ? envAtt : envRel);

        // Delay: the knob's manual half (glides), the envelope, the LFO
        const double knobLog = lnMin + (delayPot < 0.5 ? std::log (4.0) * (0.5 - delayPot) / 0.5 : 0.0);
        logDs += (knobLog - logDs) * glideK;
        double oct = 0.0;
        if (delayPot > 0.5)
        {
            const double e = env / kEnvFullV;
            oct += kEnvOct * (delayPot - 0.5) * 2.0 * (e > 1.0 ? 1.0 : e);
        }
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        const double tri = 1.0 - 4.0 * std::fabs (lfoPhase - 0.5);     // -1 .. 1
        oct += kWidthOct * width * tri;
        const double D = std::exp (logDs + oct * kLn2);
        const double clock = clampd (bits / D, kClockMinHz, kClockMaxHz);
        lastClock = clock;
        lastDelayMs = 1000.0 * bits / clock;

        // Converter input: the signal plus feedback, limited
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const double hiss = kHissV * 1.7320508 * (((noiseSeed >> 8) / 8388608.0) - 1.0);   // uniform, unit rms x kHissV
        const double u = xa + fb * yPrev + hiss;
        const double au = std::fabs (u);
        limitPeak = au > limitPeak ? au : limitPeak * limitRel;

        // Clock ticks in this sample
        tickAcc += clock / fs;
        int n = (int) tickAcc;
        tickAcc -= n;

        // Syllabic filter: the step decays with kStepTauS and is pushed up while
        // three bits in a row agree; flat out it settles at kSlopeMaxVps / clock.
        const double beta = std::exp (-1.0 / (kStepTauS * clock));
        const double inc  = (kSlopeMaxVps / clock) * (1.0 - beta);
        const double leak = std::exp (-1.0 / (kLeakTauS * clock));

        double acc = 0.0;
        const double du = n > 0 ? (u - xPrev) / n : 0.0;
        double uk = xPrev;
        for (int k = 0; k < n; ++k)
        {
            uk += du;
            bool b = false;
            if (! rep)
            {
                // Delta modulator
                b = softLimit (uk) > encEst;
                encHist = ((encHist << 1) | (b ? 1u : 0u)) & 7u;
                encStep = encStep * beta + ((encHist == 0u || encHist == 7u) ? inc : 0.0);
                if (encStep < kStepMinV) encStep = kStepMinV;
                encEst = encEst * leak + (b ? encStep : -encStep);
            }
            // Memory: read the oldest bit, write the new one (or the same
            // one back: infinite repeat)
            const bool r = readBit (pos);
            writeBit (pos, rep ? r : b);
            if (++pos >= bits) pos = 0;

            // Delta demodulator
            decHist = ((decHist << 1) | (r ? 1u : 0u)) & 7u;
            decStep = decStep * beta + ((decHist == 0u || decHist == 7u) ? inc : 0.0);
            if (decStep < kStepMinV) decStep = kStepMinV;
            decEst = decEst * leak + (r ? decStep : -decStep);
            acc += decEst;
        }
        xPrev = u;
        const double yAvg = n > 0 ? acc / n : decEst;

        // Reconstruction filter and output coupling
        double y = outHp.tick (recB.tick (recA.tick (yAvg)));
        if (pendingRange != range) { wetFade -= fadeStep; if (wetFade < 0.0) wetFade = 0.0; }
        else if (wetFade < 1.0)    { wetFade += fadeStep; if (wetFade > 1.0) wetFade = 1.0; }
        y *= wetFade;
        yPrev = y;

        const double o = (x * (1.0 - mix) + y * mix) * outG / kNominalV;
        out[i] = (float) o;
    }
}

} // namespace tbeffectron
