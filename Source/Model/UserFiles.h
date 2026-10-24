#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Kit.h"

namespace kaia
{

// Where Kaia keeps its own files. Kaia only ever writes inside this folder: never into a sample
// library, an expansion or a program file.
//   Linux:   $XDG_CONFIG_HOME/kaia (normally ~/.config/kaia)
//   macOS:   ~/Library/Application Support/Kaia
//   Windows: %APPDATA%\Kaia
// The environment variable KAIA_CONFIG_DIR overrides the location (used by the tests).
namespace UserFiles
{
    juce::File configDirectory();
    juce::File settingsFile();          // settings.xml
    juce::File presetsDirectory();      // presets/
    juce::File libraryCacheFile();      // library-index.xml

    // Kaia 1.0 used JUCE's default location (~/Kaia/Kaia.settings on Linux). Moves that file
    // into the config directory and removes the old folder if nothing else is in it.
    void migrateLegacySettings();

    // True when `f` is inside the config directory.
    bool isInsideConfigDirectory (const juce::File& f);
}

// Settings shared by every Kaia instance in the process (message thread only).
// Use through juce::SharedResourcePointer<SharedSettings>.
class SharedSettings
{
public:
    SharedSettings();

    // Re-reads the file first if another process (a DAW running plugins out of process, the
    // standalone app) saved it since, so this process never overwrites newer values.
    juce::PropertiesFile& get();

private:
    std::unique_ptr<juce::PropertiesFile> props;
    juce::Time knownModTime;
};

// User presets: a kit saved as XML in the presets folder. Samples stay where they are; the preset
// only references them, so loading a preset never touches the library.
namespace Presets
{
    constexpr const char* kExtension = ".kaiapreset";

    struct Info
    {
        juce::File file;
        juce::String name;
    };

    std::vector<Info> list();
    juce::File fileFor (const juce::String& presetName);   // presets/<safe name>.kaiapreset
    bool save (const Kit& kit, const juce::String& presetName, juce::String& error, juce::File* savedAs = nullptr);
    bool load (const juce::File& file, Kit& kit, juce::String& error);
    bool remove (const juce::File& file);                  // only deletes files inside the presets folder
    bool isPresetFile (const juce::File& file);
}

}
