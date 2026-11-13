// EffectronVST.cpp
// VST 2.4 wrapper for TB Effectron (the DeltaLab Effectron I, ADM 1020).
//
// - Parameters follow the unit's front panel (Level, Feedback, the four-way
//   delay range, Delay, Width, Speed, Mix), then Repeat, then the plugin-only
//   Input and Output trims.
// - Stereo in, stereo out: each side runs through its own copy of the unit
//   with the same settings and the LFOs in step. A mono source (both inputs
//   identical) is processed once and the second copy kept in sync.
// - On the Mac, a custom panel (PanelView.mm); elsewhere the host shows
//   generic controls.
// - No latency; a long tail (feedback and infinite repeat).
// - Plain C++98; the editor uses only Cocoa APIs present in Mac OS X 10.8.5.

#include "vst2_abi.h"
#include "Params.h"
#include "Editor.h"
#include "Presets.h"
#include "../DSP/Effectron.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
  #define TBVS_HAVE_SSE 1
#endif

namespace
{

const VstInt32 kUniqueID    = VST_FOURCC ('T', 'B', 'e', 'f');
const VstInt32 kVersion     = 1000;       // 1.0.0
const VstInt32 kNumChannels = 2;
const char* const kPluginName = "TB Effectron";

using namespace tbeffectron;

void copyString (char* dst, const char* src, size_t maxLen)
{
    std::strncpy (dst, src, maxLen);
    dst[maxLen] = 0;
}

class EffectronVST : public EditorHost
{
public:
    explicit EffectronVST (audioMasterCallback hostCallback)
        : host (hostCallback), sampleRate (44100.0), editor (0)
    {
        std::memset (&effect, 0, sizeof (effect));
        effect.magic = kEffectMagic;
        effect.dispatcher = &dispatcherProc;
        effect.process = &processAccumulateProc;
        effect.setParameter = &setParameterProc;
        effect.getParameter = &getParameterProc;
        effect.processReplacing = &processReplacingProc;
        effect.processDoubleReplacing = 0;
        effect.numPrograms = 1;
        effect.numParams = kNumParams;
        effect.numInputs = kNumChannels;
        effect.numOutputs = kNumChannels;
        effect.flags = effFlagsCanReplacing;
#if defined(__APPLE__)
        effect.flags |= effFlagsHasEditor;
#endif
        effect.initialDelay = Effectron::latencySamples();
        effect.ioRatio = 1.0f;
        effect.object = this;
        effect.uniqueID = kUniqueID;
        effect.version = kVersion;

        for (int i = 0; i < kNumParams; ++i)
            params[i] = kDefaults[i];

        copyString (programName, "Default", kVstMaxProgNameLen);
        applyAllParams();
        prepare();
    }

    ~EffectronVST()
    {
#if defined(__APPLE__)
        if (editor) editorClose (editor);
#endif
    }

    AEffect* getAEffect() { return &effect; }

    //==========================================================================
    // EditorHost
    float getParam (int index) const
    {
        return (index >= 0 && index < kNumParams) ? params[index] : 0.0f;
    }
    void getParamDisplay (int index, char* text) const
    {
        if (index >= 0 && index < kNumParams) formatParam (index, text);
        else text[0] = 0;
    }
    float getMeter (int which) const
    {
        if (which == kMeterLimit)
            return (float) std::max (unit[0].limitLevel(), unit[1].limitLevel());
        return 0.0f;
    }
    void guiBeginEdit (int index) { if (host) host (&effect, audioMasterBeginEdit, index, 0, 0, 0.0f); }
    void guiEndEdit (int index)   { if (host) host (&effect, audioMasterEndEdit, index, 0, 0, 0.0f); }
    void guiSetParam (int index, float value)
    {
        setParameter (index, value);
        if (host && index >= 0 && index < kNumParams)
            host (&effect, audioMasterAutomate, index, 0, 0, params[index]);
    }

    void refreshPresets() { presets.load(); }
    bool presetExists (int slot) const { return presets.has (slot); }
    bool savePreset (int slot)
    {
        presets.load();                 // keep slots saved by other instances
        presets.put (slot, params);
        return presets.save();
    }
    void presetName (int slot, char* text, int size) const
    {
        if (! text || size <= 0) return;
        const std::string n = presets.name (slot);
        std::strncpy (text, n.c_str(), (size_t) size - 1);
        text[size - 1] = 0;
    }
    bool renamePreset (int slot, const char* name)
    {
        presets.load();                 // keep slots saved by other instances
        if (! presets.has (slot)) return false;
        presets.setName (slot, name ? name : "");
        return presets.save();
    }
    bool loadPreset (int slot)
    {
        float v[kNumParams];
        if (! presets.get (slot, v)) return false;
        for (int i = 0; i < kNumParams; ++i)
        {
            guiBeginEdit (i);
            guiSetParam (i, v[i]);
            guiEndEdit (i);
        }
        return true;
    }

private:
    //==========================================================================
    static EffectronVST* self (AEffect* e) { return static_cast<EffectronVST*> (e->object); }

    static VstIntPtr VST_CALL dispatcherProc (AEffect* e, VstInt32 opcode, VstInt32 index,
                                              VstIntPtr value, void* ptr, float opt)
    {
        EffectronVST* p = self (e);
        if (opcode == effClose)
        {
            delete p;
            return 1;
        }
        return p->dispatch (opcode, index, value, ptr, opt);
    }

    static void VST_CALL setParameterProc (AEffect* e, VstInt32 index, float value)
    {
        self (e)->setParameter (index, value);
    }

    static float VST_CALL getParameterProc (AEffect* e, VstInt32 index)
    {
        EffectronVST* p = self (e);
        return (index >= 0 && index < kNumParams) ? p->params[index] : 0.0f;
    }

    static void VST_CALL processReplacingProc (AEffect* e, float** in, float** out, VstInt32 n)
    {
        self (e)->processReplacing (in, out, n);
    }

    static void VST_CALL processAccumulateProc (AEffect* e, float** in, float** out, VstInt32 n)
    {
        EffectronVST* p = self (e);
        float* tmp[kNumChannels];
        for (int c = 0; c < kNumChannels; ++c)
            tmp[c] = p->scratch[c];

        VstInt32 done = 0;
        while (done < n)
        {
            const VstInt32 chunk = (n - done) < kScratch ? (n - done) : kScratch;
            float* ins[kNumChannels];
            for (int c = 0; c < kNumChannels; ++c)
                ins[c] = in[c] + done;
            p->processReplacing (ins, tmp, chunk);
            for (int c = 0; c < kNumChannels; ++c)
                for (VstInt32 i = 0; i < chunk; ++i)
                    out[c][done + i] += tmp[c][i];
            done += chunk;
        }
    }

    //==========================================================================
    VstIntPtr dispatch (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
    {
        switch (opcode)
        {
            case effOpen:              return 0;
            case effSetProgram:        return 0;
            case effGetProgram:        return 0;
            case effSetProgramName:
                if (ptr) copyString (programName, (const char*) ptr, kVstMaxProgNameLen);
                return 0;
            case effGetProgramName:
                if (ptr) copyString ((char*) ptr, programName, kVstMaxProgNameLen);
                return 0;
            case effGetProgramNameIndexed:
                if (ptr && index == 0) { copyString ((char*) ptr, programName, kVstMaxProgNameLen); return 1; }
                return 0;

            case effGetParamLabel:
                if (ptr && index >= 0 && index < kNumParams)
                {
                    const int k = kParamKind[index];
                    const char* lab = "";
                    if (k == kDb12 || k == kDb24) lab = "dB";
                    else if (k == kSpeedHz) lab = "Hz";
                    else if (k == kPercent || k == kBipolar || k == kMixPct) lab = "%";
                    else if (k == kDelayMs && ! Effectron::envelopeMode (params[index])) lab = "ms";
                    copyString ((char*) ptr, lab, kVstMaxParamStrLen);
                }
                return 0;
            case effGetParamDisplay:
                if (ptr && index >= 0 && index < kNumParams)
                    formatParam (index, (char*) ptr);
                return 0;
            case effGetParamName:
                if (ptr && index >= 0 && index < kNumParams)
                    copyString ((char*) ptr, kParamNames[index], kVstMaxParamStrLen);
                return 0;
            case effCanBeAutomated:
                return (index >= 0 && index < kNumParams) ? 1 : 0;

            case effSetSampleRate:
                if (opt > 0.0f) sampleRate = opt;
                return 0;
            case effSetBlockSize:
                return 0;
            case effMainsChanged:
                if (value) prepare();
                return 0;

#if defined(__APPLE__)
            case effEditGetRect:
                if (ptr)
                {
                    editRect.top = 0; editRect.left = 0;
                    editRect.bottom = (short) kEditorHeight; editRect.right = (short) kEditorWidth;
                    *(ERect**) ptr = &editRect;
                    return 1;
                }
                return 0;
            case effEditOpen:
                if (editor) { editorClose (editor); editor = 0; }
                if (ptr) editor = editorOpen (ptr, this);
                return editor ? 1 : 0;
            case effEditClose:
                if (editor) { editorClose (editor); editor = 0; }
                return 0;
            case effEditIdle:
                return 0;
#endif
            case effGetPlugCategory:   return kPlugCategEffect;
            case effGetEffectName:
                if (ptr) copyString ((char*) ptr, kPluginName, kVstMaxEffectNameLen);
                return 1;
            case effGetProductString:
                if (ptr) copyString ((char*) ptr, kPluginName, kVstMaxProductStrLen);
                return 1;
            case effGetVendorString:
                if (ptr) copyString ((char*) ptr, "TB", kVstMaxVendorStrLen);
                return 1;
            case effGetVendorVersion:  return kVersion;
            case effGetVstVersion:     return 2400;

            case effCanDo:
                if (ptr)
                {
                    const char* s = (const char*) ptr;
                    if (! std::strcmp (s, "plugAsChannelInsert") || ! std::strcmp (s, "plugAsSend"))
                        return 1;
                    if (! std::strcmp (s, "receiveVstEvents") || ! std::strcmp (s, "receiveVstMidiEvent")
                        || ! std::strcmp (s, "sendVstEvents") || ! std::strcmp (s, "sendVstMidiEvent"))
                        return -1;
                }
                return 0;

            case effGetTailSize:       return (VstIntPtr) (sampleRate * 10.0);   // long feedback; REPEAT holds forever
            case effStartProcess:      return 0;
            case effStopProcess:       return 0;
            default:                   return 0;
        }
    }

    void formatParam (int index, char* text) const
    {
        char buf[32];
        const float v = params[index];
        switch (kParamKind[index])
        {
            case kKnob:     std::snprintf (buf, sizeof (buf), "%.1f", v * 10.0); break;
            case kBipolar:  std::snprintf (buf, sizeof (buf), "%+.0f", Effectron::feedbackGain (v) * 100.0); break;
            case kRangeSel:
            {
                static const char* const names[4] = { "Flange", "Double", "ShEcho", "LgEcho" };
                std::strcpy (buf, names[rangeIndex (v)]);
                break;
            }
            case kDelayMs:
                if (Effectron::envelopeMode (v))
                    std::snprintf (buf, sizeof (buf), "Env %.0f", (v - 0.5) * 200.0);
                else
                {
                    const double ms = Effectron::manualDelayMs (rangeIndex (params[kRange]), v);
                    std::snprintf (buf, sizeof (buf), ms < 100.0 ? "%.1f" : "%.0f", ms);
                }
                break;
            case kPercent:  std::snprintf (buf, sizeof (buf), "%.0f", v * 100.0); break;
            case kSpeedHz:
            {
                const double hz = Effectron::lfoHz (v);
                std::snprintf (buf, sizeof (buf), hz < 10.0 ? "%.2f" : "%.1f", hz);
                break;
            }
            case kMixPct:   std::snprintf (buf, sizeof (buf), "%.0f", v * 100.0); break;
            case kSwitch:   std::strcpy (buf, isOn (v) ? "On" : "Off"); break;
            case kDb12:     std::snprintf (buf, sizeof (buf), "%+.1f", (v * 2.0 - 1.0) * kInputRangeDb); break;
            case kDb24:     std::snprintf (buf, sizeof (buf), "%+.1f", (v * 2.0 - 1.0) * kOutputRangeDb); break;
            default:        buf[0] = 0; break;
        }
        copyString (text, buf, kVstMaxParamStrLen);
    }

    void setParameter (VstInt32 index, float value)
    {
        if (index < 0 || index >= kNumParams) return;
        if (! (value == value)) return;   // NaN guard
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        params[index] = value;
        applyParam (index);
    }

    void applyParam (int index)
    {
        const double v = params[index];
        const bool on = isOn (params[index]);
        for (int c = 0; c < kNumChannels; ++c)
        {
            Effectron& u = unit[c];
            switch (index)
            {
                case kLevel:    u.setLevel (v); break;
                case kFeedback: u.setFeedback (v); break;
                case kRange:    u.setRange (rangeIndex (params[index])); break;
                case kDelay:    u.setDelay (v); break;
                case kWidth:    u.setWidth (v); break;
                case kSpeed:    u.setSpeed (v); break;
                case kMix:      u.setMix (v); break;
                case kRepeat:   u.setRepeat (on); break;
                case kInput:    u.setInputGainDb ((v * 2.0 - 1.0) * kInputRangeDb); break;
                case kOutput:   u.setOutputGainDb ((v * 2.0 - 1.0) * kOutputRangeDb); break;
                default: break;
            }
        }
    }

    void applyAllParams()
    {
        for (int i = 0; i < kNumParams; ++i)
            applyParam (i);
    }

    void prepare()
    {
        for (int c = 0; c < kNumChannels; ++c)
            unit[c].prepare (sampleRate);
    }

    void processReplacing (float** in, float** out, VstInt32 n)
    {
        if (n <= 0 || ! in || ! out) return;

#if TBVS_HAVE_SSE
        const unsigned int oldCsr = _mm_getcsr();
        _mm_setcsr (oldCsr | 0x8040);   // flush denormals to zero
#endif
        // Inputs may alias the outputs (in-place): process in chunks through scratch
        VstInt32 done = 0;
        while (done < n)
        {
            const VstInt32 chunk = (n - done) < kScratch ? (n - done) : kScratch;
            std::memcpy (inBuf[0], in[0] + done, (size_t) chunk * sizeof (float));
            std::memcpy (inBuf[1], in[1] + done, (size_t) chunk * sizeof (float));
            const bool mono = in[0] == in[1]
                || std::memcmp (inBuf[0], inBuf[1], (size_t) chunk * sizeof (float)) == 0;
            unit[0].process (inBuf[0], out[0] + done, (int) chunk);
            if (mono)
            {
                if (out[1] != out[0])
                    std::memcpy (out[1] + done, out[0] + done, (size_t) chunk * sizeof (float));
                unit[1] = unit[0];      // keep the right side's unit in step (memory included)
            }
            else
                unit[1].process (inBuf[1], out[1] + done, (int) chunk);
            done += chunk;
        }

#if TBVS_HAVE_SSE
        _mm_setcsr (oldCsr);
#endif
    }

    //==========================================================================
    enum { kScratch = 512 };

    AEffect effect;
    audioMasterCallback host;
    double sampleRate;
    float params[kNumParams];
    char programName[kVstMaxProgNameLen + 1];
    Effectron unit[kNumChannels];
    void* editor;
    ERect editRect;
    PresetStore presets;
    float scratch[kNumChannels][kScratch];   // processAccumulate's outputs
    float inBuf[kNumChannels][kScratch];     // processReplacing's inputs (in-place safe)
};

} // namespace

//==============================================================================
VST_EXPORT AEffect* VSTPluginMain (audioMasterCallback host)
{
    if (! host)
        return 0;
    EffectronVST* plugin = new EffectronVST (host);
    return plugin->getAEffect();
}

#if defined(__APPLE__)
VST_EXPORT AEffect* main_macho (audioMasterCallback host)
{
    return VSTPluginMain (host);
}
#endif
