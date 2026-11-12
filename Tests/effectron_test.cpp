// effectron_test.cpp
// Checks the Effectron I model against the panel and DeltaLab's published
// specifications, then renders demo WAVs.
//
//   effectron_test [render dir]

#include "../Source/DSP/Effectron.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

using namespace tbeffectron;

static const int kSr = 44100;
static const double kTwoPi = 6.283185307179586;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double rmsRange (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (to - from)) : 0.0;
}

static double peakRange (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static double db (double a, double b) { return 20.0 * std::log10 ((a + 1e-15) / (b + 1e-15)); }

static std::vector<float> sine (double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (kTwoPi * f * i / kSr));
    return v;
}

static std::vector<float> run (Effectron& e, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 512)
    {
        const int n = (int) std::min ((size_t) 512, in.size() - pos);
        e.process (&in[pos], &out[pos], n);
    }
    return out;
}

static void defaults (Effectron& e)
{
    e.setLevel (0.575); e.setFeedback (0.5); e.setRange (kShortEcho); e.setDelay (0.25);
    e.setWidth (0.0); e.setSpeed (0.4); e.setMix (0.5); e.setRepeat (false);
    e.setInputGainDb (0.0); e.setOutputGainDb (0.0);
}

// Fit A sin + B cos at f over [from, to): amplitude and residual (dB re the fit)
static double fitSine (const std::vector<float>& y, size_t from, size_t to, double f, double* residualDb)
{
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = kTwoPi * f * i / kSr, s = std::sin (w), c = std::cos (w);
        ss += s * s; cc += c * c; sc += s * c; ys += y[i] * s; yc += y[i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    if (residualDb)
    {
        double res = 0, sig = 0;
        for (size_t i = from; i < to; ++i)
        {
            const double w = kTwoPi * f * i / kSr, fit = A * std::sin (w) + B * std::cos (w);
            res += (y[i] - fit) * (y[i] - fit); sig += fit * fit;
        }
        *residualDb = 10.0 * std::log10 ((res + 1e-30) / (sig + 1e-30));
    }
    return std::sqrt (A * A + B * B);
}

// Rising zero crossings per second over a window: the dominant pitch
static double pitchHz (const std::vector<float>& y, size_t from, size_t to)
{
    int n = 0; long first = -1, last = -1;
    for (size_t i = from + 1; i < to; ++i)
        if (y[i - 1] <= 0.0f && y[i] > 0.0f) { if (first < 0) first = (long) i; last = (long) i; ++n; }
    return (n > 1) ? (n - 1) * (double) kSr / (last - first) : 0.0;
}

// Time of the first echo of a short 2 kHz burst played at 0.1 s, in ms
static double echoMs (int range, double pot)
{
    Effectron* e = new Effectron();
    defaults (*e);
    e->setRange (range); e->setDelay (pot); e->setMix (1.0);
    e->prepare (kSr);
    std::vector<float> in ((size_t) (kSr * 1.4), 0.0f);
    const size_t at = (size_t) (0.1 * kSr);
    for (int i = 0; i < 200; ++i) in[at + i] = (float) (0.3 * std::sin (kTwoPi * 2000.0 * i / kSr));
    std::vector<float> out = run (*e, in);
    delete e;
    const double pk = peakRange (out, at, out.size());
    for (size_t i = at; i < out.size(); ++i)
        if (std::fabs (out[i]) > 0.3 * pk) return (i - at) * 1000.0 / kSr;
    return -1.0;
}

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const unsigned dataBytes = (unsigned) x.size() * 2, riff = 36 + dataBytes;
    const unsigned short fmt = 1, ch = 1, bits = 16, align = 2;
    const unsigned rate = (unsigned) sr, bps = rate * 2, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i] * 0.9;
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        const short s = (short) std::floor (v * 32767.0 + 0.5);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

// A decaying plucked string (Karplus-Strong), added into buf at start
static void pluck (std::vector<float>& buf, size_t start, double freq, double amp, double dur)
{
    const int period = std::max (2, (int) (kSr / freq + 0.5));
    std::vector<double> line (period);
    unsigned seed = (unsigned) (freq * 1000.0) + (unsigned) start;
    for (int i = 0; i < period; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((seed >> 9) / 4194304.0 - 1.0) * amp;
    }
    const size_t n = (size_t) (dur * kSr);
    int idx = 0;
    double prev = 0.0;
    for (size_t i = 0; i < n && start + i < buf.size(); ++i)
    {
        const double cur = line[idx];
        const double next = 0.996 * 0.5 * (cur + prev);
        prev = cur;
        line[idx] = next;
        buf[start + i] += (float) cur;
        if (++idx >= period) idx = 0;
    }
}

static std::vector<float> makeChords (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double chords[4][4] = { { 82.4, 123.5, 164.8, 207.7 }, { 110.0, 164.8, 220.0, 277.2 },
                                  { 98.0, 146.8, 196.0, 246.9 }, { 73.4, 110.0, 146.8, 185.0 } };
    for (int c = 0; c * 2.0 < seconds; ++c)
        for (int s = 0; s < 4; ++s)
            pluck (v, (size_t) ((c * 2.0 + s * 0.012) * kSr), chords[c % 4][s], 0.22, 2.0);
    return v;
}

static std::vector<float> makeRiff (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double notes[8] = { 82.4, 98.0, 110.0, 123.5, 110.0, 98.0, 146.8, 130.8 };
    for (int i = 0; i * 0.25 < seconds; ++i)
        pluck (v, (size_t) (i * 0.25 * kSr), notes[i % 8], 0.35, 0.6);
    return v;
}

int main (int argc, char** argv)
{
    const std::string renderDir = argc > 1 ? argv[1] : "";
    setvbuf (stdout, 0, _IONBF, 0);

    //--------------------------------------------------------------------------
    std::printf ("Delay ranges (DIGITAL DELAY buttons, DELAY knob)\n");
    {
        bool all = true;
        for (int r = 0; r < kNumRanges; ++r)
        {
            const double pots[3] = { 0.0, 0.25, 0.5 };
            for (int k = 0; k < 3; ++k)
            {
                const double want = Effectron::manualDelayMs (r, pots[k]);
                const double got = echoMs (r, pots[k]);
                std::printf ("    range %d, DELAY %.2f: %.2f ms (set %.2f)\n", r, pots[k], got, want);
                if (std::fabs (got - want) > 0.02 * want + 0.3) all = false;
            }
        }
        check (all, "echo times within 2 % of the setting in every range");
        check (Effectron::manualDelayMs (kFlange, 0.5) == 4.0 && Effectron::manualDelayMs (kFlange, 0.0) == 16.0
               && Effectron::manualDelayMs (kDouble, 0.5) == 16.0 && Effectron::manualDelayMs (kDouble, 0.0) == 64.0
               && Effectron::manualDelayMs (kShortEcho, 0.5) == 64.0 && Effectron::manualDelayMs (kShortEcho, 0.0) == 256.0
               && Effectron::manualDelayMs (kLongEcho, 0.5) == 256.0 && Effectron::manualDelayMs (kLongEcho, 0.0) == 1024.0,
               "ranges as on the panel: 4-16, 16-64, 64-256, 256-1024 ms");
        bool clocks = true;
        for (int r = 0; r < kNumRanges; ++r)
        {
            const double fMin = Effectron::memoryBits (r) / (Effectron::manualDelayMs (r, 0.0) * 0.001);
            const double fMax = Effectron::memoryBits (r) / (Effectron::manualDelayMs (r, 0.5) * 0.001);
            if (std::fabs (fMin - 1.024e6) > 1.0 || std::fabs (fMax - 4.096e6) > 1.0) clocks = false;
        }
        check (clocks, "ADM clock 1.024-4.096 MHz across every range");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nLevels, bandwidth, noise (MIX at EFFECT)\n");
    {
        double res = 0.0;
        double g1k = 0, g10k = 0, g18k = 0;
        const double fs[3] = { 1000.0, 10000.0, 18000.0 };
        double* gs[3] = { &g1k, &g10k, &g18k };
        double thdMin = 0.0, thdMax = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            Effectron e; defaults (e); e.setRange (kFlange); e.setDelay (0.5); e.setMix (1.0); e.prepare (kSr);
            std::vector<float> in = sine (fs[k], 0.15, 0.6);
            std::vector<float> out = run (e, in);
            *gs[k] = db (fitSine (out, kSr / 3, out.size(), fs[k], k == 0 ? &res : 0), 0.15);
        }
        thdMin = res;
        {
            Effectron e; defaults (e); e.setRange (kFlange); e.setDelay (0.0); e.setMix (1.0); e.prepare (kSr);
            std::vector<float> out = run (e, sine (1000.0, 0.15, 0.6));
            fitSine (out, kSr / 3, out.size(), 1000.0, &thdMax);
        }
        std::printf ("    gain at 1 kHz %.2f dB, 10 kHz %.2f dB, 18 kHz %.2f dB\n", g1k, g10k, g18k);
        std::printf ("    noise + distortion at 1 kHz (0.3 V): %.1f dB at the fastest clock, %.1f dB at the slowest\n", thdMin, thdMax);
        check (std::fabs (g1k) < 0.5, "unity gain through the delay at the default LEVEL");
        check (g10k > -4.0 && g18k < -20.0, "15 kHz bandwidth: 10 kHz within 4 dB, 18 kHz down 20 dB");
        check (thdMin < -55.0 && thdMax < -45.0, "a clean, not sterile, digital delay");

        Effectron e; defaults (e); e.setMix (1.0); e.prepare (kSr);
        std::vector<float> z ((size_t) (kSr * 2), 0.0f);
        std::vector<float> out = run (e, z);
        const double n = db (rmsRange (out, kSr, out.size()), 1.0);
        std::printf ("    idle noise %.1f dB (re 1.0 output)\n", n);
        check (n < -82.0 && n > -95.0, "idle noise about -87 dB (DeltaLab's dynamic range)");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nINPUT LEVEL and LIMIT\n");
    {
        Effectron e; defaults (e); e.prepare (kSr);
        run (e, sine (440.0, 0.5, 0.3));
        const double normal = e.limitLevel();
        Effectron f; defaults (f); f.setLevel (0.9); f.prepare (kSr);
        run (f, sine (440.0, 0.5, 0.3));
        const double hot = f.limitLevel();
        std::printf ("    converter peak %.2f V at the default LEVEL, %.2f V with LEVEL at 9\n", normal, hot);
        check (normal < Effectron::limitThreshold() && hot > Effectron::limitThreshold(), "LIMIT lights only when driven hard");
        check (Effectron::levelGain (0.0) == 0.0 && Effectron::levelGain (1.0) > Effectron::levelGain (0.6), "LEVEL from 0 to MAX");

        Effectron g; defaults (g); g.setLevel (1.0); g.setMix (1.0); g.prepare (kSr);
        std::vector<float> out = run (g, sine (440.0, 1.0, 0.5));
        check (allFinite (out, 2.0), "full LEVEL with a loud input limits cleanly");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nMIX (SOURCE / EQUAL / EFFECT)\n");
    {
        Effectron e; defaults (e); e.setMix (0.0); e.prepare (kSr);
        std::vector<float> in = sine (330.0, 0.3, 0.5);
        std::vector<float> out = run (e, in);
        double maxd = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxd = std::max (maxd, (double) std::fabs (out[i] - in[i]));
        std::printf ("    SOURCE: largest difference from the input %.2g\n", maxd);
        check (maxd < 0.01, "SOURCE: the input, at unity at the default LEVEL");

        // EFFECT on a burst: nothing until the echo arrives
        Effectron f; defaults (f); f.setMix (1.0); f.prepare (kSr);
        std::vector<float> b ((size_t) (kSr * 0.5), 0.0f);
        for (int i = 0; i < 2000; ++i) b[i + 100] = (float) (0.3 * std::sin (kTwoPi * 500.0 * i / kSr));
        std::vector<float> o = run (f, b);
        check (peakRange (o, 100, 2100) < 0.01 && peakRange (o, (size_t) (0.128 * kSr), (size_t) (0.2 * kSr)) > 0.2,
               "EFFECT: only the delayed signal");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nFEEDBACK (NEG / POS)\n");
    {
        double pkPos[3], signPos = 0.0, signNeg = 0.0;
        const double pots[2] = { 0.85, 0.15 };
        for (int s = 0; s < 2; ++s)
        {
            Effectron e; defaults (e); e.setRange (kDouble); e.setDelay (0.5); e.setFeedback (pots[s]); e.setMix (1.0); e.prepare (kSr);
            std::vector<float> in ((size_t) (kSr * 0.5), 0.0f);
            for (int i = 0; i < 40; ++i) in[1000 + i] = (float) (0.5 * std::sin (3.14159265358979 * i / 40.0));   // a short positive bump
            std::vector<float> out = run (e, in);
            // echoes every 16 ms: compare the 1st and 2nd echo's sign and size
            const size_t d = (size_t) (0.016 * kSr);
            double first = 0.0, second = 0.0;
            for (size_t i = 1000 + d - 40; i < 1000 + d + 120; ++i) if (std::fabs (out[i]) > std::fabs (first)) first = out[i];
            for (size_t i = 1000 + 2 * d - 40; i < 1000 + 2 * d + 160; ++i) if (std::fabs (out[i]) > std::fabs (second)) second = out[i];
            if (s == 0) { pkPos[0] = first; pkPos[1] = second; signPos = first * second; }
            else signNeg = first * second;
            std::printf ("    feedback %s: 1st echo %+.3f, 2nd %+.3f\n", s == 0 ? "POS 70%" : "NEG 70%", first, second);
        }
        check (signPos > 0.0 && signNeg < 0.0, "POS repeats keep their polarity, NEG flips each repeat");
        check (std::fabs (pkPos[1]) < std::fabs (pkPos[0]) && std::fabs (pkPos[1]) > 0.5 * std::fabs (pkPos[0]),
               "repeats decay at 70 % feedback");

        // Full POS: rings a long time but stays bounded
        Effectron e; defaults (e); e.setRange (kShortEcho); e.setDelay (0.5); e.setFeedback (1.0); e.setMix (1.0); e.prepare (kSr);
        std::vector<float> in ((size_t) (kSr * 6), 0.0f);
        for (int i = 0; i < kSr / 4; ++i) in[i] = (float) (0.4 * std::sin (kTwoPi * 300.0 * i / kSr));
        std::vector<float> out = run (e, in);
        const double late = rmsRange (out, (size_t) (5 * kSr), out.size());
        std::printf ("    full POS feedback after 5 s: %.3f rms, peak %.2f\n", late, peakRange (out, 0, out.size()));
        check (allFinite (out, 2.0) && late > 0.01, "full feedback keeps repeating without running away");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nREPEAT (infinite repeat) and pitch from the DELAY knob\n");
    {
        Effectron* e = new Effectron(); defaults (*e);
        e->setRange (kShortEcho); e->setDelay (0.25); e->setMix (1.0); e->prepare (kSr);
        std::vector<float> in = sine (500.0, 0.3, 0.5);
        run (*e, in);
        e->setRepeat (true);
        std::vector<float> z ((size_t) (kSr * 3), 0.0f);
        std::vector<float> held = run (*e, z);
        const double a0 = rmsRange (held, 0, kSr / 2), a1 = rmsRange (held, (size_t) (2.5 * kSr), held.size());
        const double p0 = pitchHz (held, (size_t) (2.0 * kSr), held.size());
        std::printf ("    held loop: %.3f rms at the start, %.3f after 2.5 s, pitch %.1f Hz\n", a0, a1, p0);
        check (a1 > 0.1 && std::fabs (db (a1, a0)) < 1.0, "the loop holds without decaying");
        check (std::fabs (p0 - 500.0) < 5.0, "and keeps its pitch");
        // DELAY from halfway (128 ms) to MIN (64 ms): the clock doubles, the loop goes up an octave
        e->setDelay (0.5);
        std::vector<float> up = run (*e, z);
        const double p1 = pitchHz (up, (size_t) (1.0 * kSr), up.size());
        std::printf ("    DELAY to MIN: pitch %.1f Hz\n", p1);
        check (std::fabs (p1 - 1000.0) < 15.0, "DELAY to MIN plays the loop an octave up");
        e->setRepeat (false);
        e->setDelay (0.25);
        std::vector<float> after = run (*e, z);
        check (rmsRange (after, (size_t) (2.0 * kSr), after.size()) < 0.01, "REPEAT off: the loop is replaced by the (silent) input");
        delete e;
    }

    //--------------------------------------------------------------------------
    std::printf ("\nMODULATOR: WIDTH, SPEED, envelope\n");
    {
        Effectron e; defaults (e); e.setRange (kFlange); e.setDelay (0.25); e.setWidth (1.0); e.setSpeed (0.6); e.prepare (kSr);
        std::vector<float> in = sine (200.0, 0.1, 3.0);
        double lo = 1e9, hi = 0.0;
        std::vector<float> out (in.size());
        for (size_t pos = 0; pos < in.size(); pos += 64)
        {
            e.process (&in[pos], &out[pos], (int) std::min ((size_t) 64, in.size() - pos));
            if (pos > (size_t) kSr / 2) { lo = std::min (lo, e.currentDelayMs()); hi = std::max (hi, e.currentDelayMs()); }
        }
        std::printf ("    WIDTH 100%%, centre 8 ms: sweeps %.2f .. %.2f ms (ratio %.1f)\n", lo, hi, hi / lo);
        check (hi / lo > 7.0 && hi / lo < 8.5, "WIDTH 100 % sweeps 8:1");
        check (std::fabs (Effectron::lfoHz (0.0) - 0.05) < 1e-9 && std::fabs (Effectron::lfoHz (1.0) - 12.0) < 1e-9, "SPEED 0.05 .. 12 Hz");

        Effectron w; defaults (w); w.setRange (kFlange); w.setDelay (0.25); w.setWidth (0.0); w.prepare (kSr);
        run (w, in);
        check (std::fabs (w.currentDelayMs() - 8.0) < 0.05, "WIDTH 0 %: no sweep");

        // Envelope: DELAY turned past 12 o'clock
        Effectron v; defaults (v); v.setRange (kDouble); v.setDelay (1.0); v.prepare (kSr);
        std::vector<float> quiet ((size_t) (kSr / 2), 0.0f);
        run (v, quiet);
        const double dq = v.currentDelayMs();
        run (v, sine (220.0, 0.5, 0.3));
        const double dl = v.currentDelayMs();
        std::printf ("    envelope at full: %.1f ms quiet, %.1f ms playing loud\n", dq, dl);
        check (std::fabs (dq - 16.0) < 0.5 && dl > 40.0, "envelope follower: louder playing, longer delay");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nRange buttons while playing, automation, stability\n");
    {
        Effectron* e = new Effectron(); defaults (*e); e->setMix (0.5); e->prepare (kSr);
        std::vector<float> in = sine (220.0, 0.3, 2.0);
        std::vector<float> out (in.size());
        double jump = 0.0;
        for (size_t pos = 0; pos < in.size(); pos += 512)
        {
            if (pos % (kSr / 4) < 512) e->setRange ((int) (pos / (kSr / 4)) % 4);
            e->process (&in[pos], &out[pos], (int) std::min ((size_t) 512, in.size() - pos));
        }
        for (size_t i = 1; i < out.size(); ++i) jump = std::max (jump, (double) std::fabs (out[i] - out[i - 1]));
        std::printf ("    largest sample step while switching ranges: %.3f\n", jump);
        check (jump < 0.1, "range buttons switch without clicks");
        delete e;

        // Random settings x levels x frequencies
        unsigned seed = 7u;
        bool stable = true;
        int runs = 0;
        for (int t = 0; t < 60; ++t)
        {
            Effectron* x = new Effectron();
            double v[8];
            for (int k = 0; k < 8; ++k) { seed = seed * 1664525u + 1013904223u; v[k] = (seed >> 8) / 16777216.0; }
            if (t % 3 == 0) for (int k = 0; k < 8; ++k) v[k] = v[k] < 0.5 ? 0.0 : 1.0;   // corners
            x->setLevel (v[0]); x->setFeedback (v[1]); x->setRange ((int) (v[2] * 3.99)); x->setDelay (v[3]);
            x->setWidth (v[4]); x->setSpeed (v[5]); x->setMix (v[6]); x->setRepeat (v[7] > 0.8);
            x->prepare (t % 2 ? 96000.0 : 44100.0);
            const double amps[3] = { 0.01, 0.5, 4.0 };
            const double freqs[3] = { 41.0, 1000.0, 15000.0 };
            std::vector<float> in = sine (freqs[t % 3], amps[(t / 3) % 3], 0.4);
            std::vector<float> o = run (*x, in);
            if (! allFinite (o, 20.0)) { stable = false; std::printf ("    unstable at setting %d\n", t); }
            ++runs;
            delete x;
        }
        check (stable, "random and corner settings stay finite and bounded");

        // Every control automated together while playing
        Effectron* a = new Effectron(); a->prepare (kSr);
        std::vector<float> src = makeRiff (3.0);
        std::vector<float> o (src.size());
        for (size_t pos = 0; pos < src.size(); pos += 64)
        {
            const double ph = (double) pos / src.size();
            a->setLevel (0.5 + 0.5 * std::sin (6.28 * ph)); a->setFeedback (0.5 + 0.5 * std::sin (12.5 * ph));
            a->setRange ((int) (ph * 8) % 4); a->setDelay (0.5 + 0.5 * std::sin (19.0 * ph));
            a->setWidth (0.5 + 0.5 * std::sin (25.0 * ph)); a->setSpeed (0.5 + 0.5 * std::sin (31.0 * ph));
            a->setMix (0.5 + 0.5 * std::sin (37.0 * ph)); a->setRepeat (std::sin (44.0 * ph) > 0.7);
            a->setInputGainDb (12.0 * std::sin (50.0 * ph)); a->setOutputGainDb (24.0 * std::sin (56.0 * ph));
            a->process (&src[pos], &o[pos], (int) std::min ((size_t) 64, src.size() - pos));
        }
        check (allFinite (o, 50.0), "all controls automated while playing");
        delete a;

        // Silence after playing settles to the noise floor
        Effectron s; defaults (s); s.setFeedback (0.8); s.prepare (kSr);
        run (s, makeRiff (1.0));
        std::vector<float> z ((size_t) (kSr * 8), 0.0f);
        std::vector<float> zo = run (s, z);
        const double tail = rmsRange (zo, (size_t) (7 * kSr), zo.size());
        std::printf ("    8 s after playing (feedback POS 60%%): %.2g rms\n", tail);
        check (tail < 1e-3, "silence settles to the noise floor");
    }

    //--------------------------------------------------------------------------
    std::printf ("\nCPU\n");
    {
        Effectron* e = new Effectron(); defaults (*e); e->setRange (kFlange); e->setDelay (0.5); e->setWidth (1.0); e->prepare (kSr);
        std::vector<float> in = makeChords (4.0);
        const std::clock_t t0 = std::clock();
        run (*e, in);
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
        std::printf ("    worst case (FLANGE at MIN, WIDTH 100%%): %.1f%% of one core per channel\n", 100.0 * secs / 4.0);
        check (secs / 4.0 < 0.25, "CPU well within budget");
        delete e;
    }

    //--------------------------------------------------------------------------
    if (! renderDir.empty())
    {
        std::printf ("\nDemo renders in %s\n", renderDir.c_str());
        struct Demo { const char* name; int range; double delay, width, speed, fb, mix; bool chords; };
        const Demo demos[] = {
            { "flange_slow",      kFlange,     0.40, 1.00, 0.30, 0.80, 0.5, true  },
            { "flange_neg_fb",    kFlange,     0.45, 0.80, 0.25, 0.15, 0.5, true  },
            { "chorus_double",    kDouble,     0.45, 0.35, 0.55, 0.50, 0.5, true  },
            { "slapback",         kShortEcho,  0.35, 0.00, 0.40, 0.50, 0.45, false },
            { "long_echo_repeats",kLongEcho,   0.30, 0.00, 0.40, 0.80, 0.45, false },
            { "envelope_double",  kDouble,     0.90, 0.00, 0.40, 0.60, 0.5, false },
        };
        for (size_t d = 0; d < sizeof (demos) / sizeof (demos[0]); ++d)
        {
            Effectron* e = new Effectron(); defaults (*e);
            e->setRange (demos[d].range); e->setDelay (demos[d].delay); e->setWidth (demos[d].width);
            e->setSpeed (demos[d].speed); e->setFeedback (demos[d].fb); e->setMix (demos[d].mix);
            e->prepare (kSr);
            std::vector<float> in = demos[d].chords ? makeChords (8.0) : makeRiff (8.0);
            std::vector<float> o = run (*e, in);
            writeWav (renderDir + "/" + demos[d].name + ".wav", o, kSr);
            std::printf ("    %s.wav\n", demos[d].name);
            delete e;
        }
        // Infinite repeat: catch a phrase, hold it, sweep DELAY to bend it
        {
            Effectron* e = new Effectron(); defaults (*e);
            e->setRange (kLongEcho); e->setDelay (0.1); e->setMix (0.5); e->prepare (kSr);
            std::vector<float> in = makeRiff (10.0);
            for (size_t i = (size_t) (4 * kSr); i < in.size(); ++i) in[i] = 0.0f;
            std::vector<float> o (in.size());
            for (size_t pos = 0; pos < in.size(); pos += 512)
            {
                const double t = (double) pos / kSr;
                if (t > 3.0) e->setRepeat (true);
                if (t > 6.0) e->setDelay (0.1 + 0.3 * std::min (1.0, (t - 6.0) / 3.0));
                e->process (&in[pos], &o[pos], (int) std::min ((size_t) 512, in.size() - pos));
            }
            writeWav (renderDir + "/repeat_pitch_bend.wav", o, kSr);
            std::printf ("    repeat_pitch_bend.wav\n");
            delete e;
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL DSP CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
