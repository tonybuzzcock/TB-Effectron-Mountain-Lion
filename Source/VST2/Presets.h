// Presets.h
// Eight user preset slots, each holding every parameter and a name, kept in one small
// text file so all instances and sessions share them:
//   Mac:   ~/Library/Application Support/TB/TB Effectron/Presets.txt
//   other: ~/.config/TB/TB Effectron/Presets.txt
// The TBEFFECTRON_PRESET_FILE environment variable overrides the location (tests).
// Plain C++98. File access happens only on the editor's thread, never in audio.

#ifndef TBEFFECTRON_PRESETS_H
#define TBEFFECTRON_PRESETS_H

#include "Params.h"

#include <string>

namespace tbeffectron
{

enum { kNumPresetSlots = 8, kMaxPresetName = 40 };

class PresetStore
{
public:
    PresetStore();

    void setPath (const std::string& p) { path = p; }
    const std::string& getPath() const { return path; }
    static std::string defaultPath();

    bool load();                    // re-reads the file; missing file = all slots empty
    bool save() const;              // writes every slot, creating folders as needed

    bool has (int slot) const;
    bool get (int slot, float values[kNumParams]) const;
    void put (int slot, const float values[kNumParams]);   // keeps the slot's name

    // A name per slot (up to kMaxPresetName characters, printable, one line)
    std::string name (int slot) const;
    void setName (int slot, const std::string& n);

private:
    std::string path;
    bool used[kNumPresetSlots];
    std::string names[kNumPresetSlots];
    float vals[kNumPresetSlots][kNumParams];
};

} // namespace tbeffectron

#endif
