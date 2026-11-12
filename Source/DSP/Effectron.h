// Effectron.h
// DeltaLab Effectron I, model ADM 1020: an adaptive delta modulation (ADM)
// digital delay from the early 1980s. There is no schematic to work from, so
// this is a behavioural model built from DeltaLab's published specifications
// and the front panel:
//
//   INPUT LEVEL -> anti-alias low-pass (15 kHz) -> soft limit (LIMIT LED)
//      -> delta modulator: one bit per clock tick, a syllabically adapting
//         step size (CVSD-style: the step grows while three bits in a row
//         agree and decays otherwise), a slightly leaky integrator
//      -> a fixed-length bit memory per range. The delay is set by changing
//         the CLOCK, not the memory length: delay = bits / clock. So sweeping
//         DELAY bends the pitch of what is already in the memory, and with
//         REPEAT on (the memory recirculating) DELAY transposes the loop.
//      -> delta demodulator (the same adaptive integrator fed from memory)
//      -> reconstruction low-pass (15 kHz) -> FEEDBACK (NEG / POS) back to
//         the input, and MIX (SOURCE / EQUAL / EFFECT) to the output.
//
//   DIGITAL DELAY buttons pick the memory length (four ranges, 4:1 each):
//      FLANGE 4-16 ms, DOUBLE 16-64 ms, SH ECHO 64-256 ms, LG ECHO 256-1024 ms.
//   DELAY (outer knob): MAX fully left, MIN at 12 o'clock; clockwise from 12
//      the envelope follower sweeps the delay with the playing (louder =
//      longer), more so the further it is turned.
//   WIDTH (inner knob, 12 to 5 o'clock): LFO sweep depth, up to 8:1 at 100%.
//   SPEED: LFO rate.
//
// The ADM runs at its own clock (about 1-4 MHz across each range, up to
// about 8 MHz at the bottom of a FLANGE sweep), so its noise, slope overload
// and pitch bending come from the process itself. Every value not fixed by
// the published specifications is a named estimate in Effectron.cpp.
//
// Volts in, volts out (1.0 = 1 V). Plain C++98, no allocation after
// construction. The bit memory lives inside the object (128 KB).

#ifndef TBEFFECTRON_EFFECTRON_H
#define TBEFFECTRON_EFFECTRON_H

#include "Filters.h"

namespace tbeffectron
{

enum Range { kFlange = 0, kDouble, kShortEcho, kLongEcho, kNumRanges };

class Effectron
{
public:
    Effectron();

    void prepare (double sampleRate);
    void reset();

    // Front panel (0..1 pot rotation), in the panel's order
    void setLevel (double v);           // INPUT LEVEL
    void setFeedback (double v);        // NEG 0 .. 0.5 off .. 1 POS
    void setRange (int range);          // DIGITAL DELAY buttons
    void setDelay (double v);           // 0 MAX .. 0.5 MIN .. 1 envelope (full)
    void setWidth (double v);           // 0 .. 100 %
    void setSpeed (double v);           // SLOW .. FAST
    void setMix (double v);             // SOURCE .. EQUAL .. EFFECT
    void setRepeat (bool on);           // infinite repeat (rear footswitch jack)
    // Plugin only
    void setInputGainDb (double dB);
    void setOutputGainDb (double dB);

    // Mono in, mono out
    void process (const float* in, float* out, int numSamples);

    static int latencySamples() { return 0; }

    // Introspection for the panel, the wrapper and the tests
    static double rangeMinMs (int range);                 // 4, 16, 64, 256
    static double manualDelayMs (int range, double delayPot);   // delay set by the knob (manual half)
    static bool   envelopeMode (double delayPot) { return delayPot > 0.5; }
    static double lfoHz (double speedPot);
    static double levelGain (double levelPot);
    static double feedbackGain (double feedbackPot);
    static int    memoryBits (int range);
    double limitLevel() const { return limitPeak; }       // converter input peak, volts
    static double limitThreshold();                       // LIMIT LED lights above this
    double currentDelayMs() const { return lastDelayMs; }
    double currentClockHz() const { return lastClock; }

private:
    void   fillIdle();
    inline bool readBit (int pos) const { return (mem[pos >> 5] >> (pos & 31)) & 1u; }
    inline void writeBit (int pos, bool b)
    {
        if (b) mem[pos >> 5] |= (1u << (pos & 31));
        else   mem[pos >> 5] &= ~(1u << (pos & 31));
    }

    enum { kMaxBits = 16384 * 64, kMemWords = kMaxBits / 32 };

    double fs;

    // Targets and smoothed values
    double levelT, level, fbT, fb, mixT, mix, inT, inG, outT, outG;
    double delayPot, widthT, width, lfoInc;
    double logDs;               // smoothed log of the set delay (seconds)
    double smK;                 // one-pole coefficient for the pots
    double glideK;              // the delay's glide
    int    range, pendingRange, bits;
    double speedPot;
    bool   repeat;
    double wetFade;             // 0..1 after a range change

    // Audio path
    Biquad aaA;  FirstOrder aaB;        // anti-alias, 15 kHz 3rd order
    Biquad recA; FirstOrder recB;       // reconstruction, 15 kHz 3rd order
    FirstOrder outHp;                   // output coupling
    double xPrev;                       // encoder input at the previous sample
    double yPrev;                       // last reconstructed sample (feedback)
    double env;                         // envelope follower
    double envAtt, envRel;
    double limitPeak, limitRel;

    // Delta modulator / demodulator
    double encEst, encStep, decEst, decStep;
    unsigned encHist, decHist;
    double tickAcc;                     // fractional clock ticks
    int    pos;                         // memory position (read, then write)
    double lfoPhase;

    double lastDelayMs, lastClock;
    unsigned noiseSeed;

    unsigned mem[kMemWords];
};

} // namespace tbeffectron

#endif
