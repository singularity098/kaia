#include "UserFiles.h"

namespace kaia
{

juce::File UserFiles::configDirectory()
{
    auto overrideDir = juce::SystemStats::getEnvironmentVariable ("KAIA_CONFIG_DIR", {});
    if (overrideDir.isNotEmpty() && juce::File::isAbsolutePath (overrideDir))
        return juce::File (overrideDir);

   #if JUCE_LINUX || JUCE_BSD
    // XDG base directory spec: $XDG_CONFIG_HOME if set to an absolute path, else ~/.config
    auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_CONFIG_HOME", {});
    auto base = xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg)
                    ? juce::File (xdg)
                    : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config");
    return base.getChildFile ("kaia");
   #elif JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Kaia");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Kaia");
   #endif
}

juce::File UserFiles::settingsFile()       { return configDirectory().getChildFile ("settings.xml"); }
juce::File UserFiles::presetsDirectory()   { return configDirectory().getChildFile ("presets"); }
juce::File UserFiles::libraryCacheFile()   { return configDirectory().getChildFile ("library-index.xml"); }

bool UserFiles::isInsideConfigDirectory (const juce::File& f)
{
    return f.isAChildOf (configDirectory());
}

void UserFiles::migrateLegacySettings()
{
    if (juce::SystemStats::getEnvironmentVariable ("KAIA_CONFIG_DIR", {}).isNotEmpty())
        return;

    // Where juce::PropertiesFile put "Kaia.settings" with folderName "Kaia"
   #if JUCE_LINUX || JUCE_BSD
    auto legacyDir = juce::File ("~/Kaia");
   #elif JUCE_MAC
    auto legacyDir = juce::File ("~/Library/Application Support/Kaia");
   #else
    auto legacyDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Kaia");
   #endif

    auto legacy = legacyDir.getChildFile ("Kaia.settings");
    if (! legacy.existsAsFile())
        return;

    auto target = settingsFile();
    if (! target.existsAsFile())
    {
        target.getParentDirectory().createDirectory();
        if (! legacy.copyFileTo (target))
            return;
    }
    legacy.deleteFile();

    // Remove the old folder only if Kaia's file was the only thing in it
    if (legacyDir != configDirectory() && legacyDir.isDirectory()
         && legacyDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles) == 0
         && legacyDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        legacyDir.deleteFile();
}

//==============================================================================
SharedSettings::SharedSettings()
{
    UserFiles::migrateLegacySettings();
    UserFiles::configDirectory().createDirectory();

    juce::PropertiesFile::Options o;
    o.applicationName = "Kaia";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;
    props = std::make_unique<juce::PropertiesFile> (UserFiles::settingsFile(), o);
    knownModTime = props->getFile().getLastModificationTime();
}

juce::PropertiesFile& SharedSettings::get()
{
    auto modTime = props->getFile().getLastModificationTime();
    if (modTime != knownModTime)
    {
        if (! props->needsToBeSaved())
            props->reload();
        knownModTime = modTime;
    }
    return *props;
}

//==============================================================================
namespace
{
    juce::String safeFileName (const juce::String& name)
    {
        auto s = juce::File::createLegalFileName (name.trim()).replaceCharacters ("/\\", "--").trim();
        while (s.startsWithChar ('.'))
            s = s.substring (1);
        return s.isEmpty() ? juce::String ("Preset") : s.substring (0, 120);
    }
}

bool Presets::isPresetFile (const juce::File& file)
{
    return file.hasFileExtension (kExtension);
}

std::vector<Presets::Info> Presets::list()
{
    std::vector<Info> result;
    auto dir = UserFiles::presetsDirectory();
    if (! dir.isDirectory())
        return result;

    for (const auto& e : juce::RangedDirectoryIterator (dir, false, juce::String ("*") + kExtension, juce::File::findFiles))
    {
        Info info;
        info.file = e.getFile();
        info.name = e.getFile().getFileNameWithoutExtension();
        result.push_back (info);
    }
    std::sort (result.begin(), result.end(), [] (const Info& a, const Info& b) { return a.name.compareNatural (b.name) < 0; });
    return result;
}

juce::File Presets::fileFor (const juce::String& presetName)
{
    return UserFiles::presetsDirectory().getChildFile (safeFileName (presetName) + kExtension);
}

bool Presets::save (const Kit& kit, const juce::String& presetName, juce::String& error, juce::File* savedAs)
{
    auto file = fileFor (presetName);
    if (! UserFiles::isInsideConfigDirectory (file))
    {
        error = "Presets can only be saved in " + UserFiles::presetsDirectory().getFullPathName();
        return false;
    }
    if (! file.getParentDirectory().createDirectory())
    {
        error = "Could not create " + file.getParentDirectory().getFullPathName();
        return false;
    }

    Kit copy = kit;
    copy.name = presetName.trim().isNotEmpty() ? presetName.trim() : file.getFileNameWithoutExtension();

    juce::XmlElement root ("KaiaPreset");
    root.setAttribute ("version", 1);
    root.setAttribute ("name", copy.name);
    root.addChildElement (copy.toXml().release());

    // Write to a temporary file first so a crash never leaves a half-written preset
    juce::TemporaryFile temp (file);
    if (! root.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    if (savedAs != nullptr)
        *savedAs = file;
    return true;
}

bool Presets::load (const juce::File& file, Kit& kit, juce::String& error)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("KaiaPreset"))
    {
        error = file.getFileName() + " is not a Kaia preset.";
        return false;
    }
    auto* kitXml = xml->getChildByName ("Kit");
    if (kitXml == nullptr)
    {
        error = file.getFileName() + " contains no kit.";
        return false;
    }
    kit = Kit::fromXml (*kitXml);
    if (kit.name.isEmpty())
        kit.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    return true;
}

bool Presets::remove (const juce::File& file)
{
    if (! isPresetFile (file) || ! file.isAChildOf (UserFiles::presetsDirectory()))
        return false;
    return file.deleteFile();
}

}
