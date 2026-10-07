// C:\workspace\Stella AI Studio\src\Project.cpp

#include "Project.h"

namespace
{
    juce::String slug (const juce::String& text)
    {
        juce::String result;

        for (auto c : text.toLowerCase())
            if (juce::CharacterFunctions::isLetterOrDigit (c) && c < 128)
                result << juce::String::charToString (c);

        return result;
    }
}

//==============================================================================
juce::String Project::kindToString (PluginKind kind)
{
    switch (kind)
    {
        case PluginKind::instrument:  return "instrument";
        case PluginKind::effect:      return "effect";
        case PluginKind::noteEffect:  return "note-effect";
    }

    return "instrument";
}

PluginKind Project::kindFromString (const juce::String& text)
{
    if (text == "effect")       return PluginKind::effect;
    if (text == "note-effect")  return PluginKind::noteEffect;

    return PluginKind::instrument;
}

juce::String Project::kindDisplayName (PluginKind kind)
{
    switch (kind)
    {
        case PluginKind::instrument:  return "Instrument";
        case PluginKind::effect:      return "Audio effect";
        case PluginKind::noteEffect:  return "MIDI effect";
    }

    return "Instrument";
}

juce::String Project::makePluginId (const juce::String& vendor, const juce::String& name)
{
    auto vendorPart = slug (vendor);
    auto namePart = slug (name);

    if (vendorPart.isEmpty())  vendorPart = "vendor";
    if (namePart.isEmpty())    namePart = "plugin";

    // A domain label can't start with a digit.
    if (juce::CharacterFunctions::isDigit (vendorPart[0]))  vendorPart = "v" + vendorPart;
    if (juce::CharacterFunctions::isDigit (namePart[0]))    namePart = "p" + namePart;

    return "com." + vendorPart + "." + namePart;
}

//==============================================================================
juce::Result Project::create (const juce::File& parentFolder, ProjectInfo newInfo)
{
    newInfo.name = newInfo.name.trim();
    newInfo.vendor = newInfo.vendor.trim();

    if (newInfo.name.isEmpty())
        return juce::Result::fail ("The plugin needs a name.");

    if (newInfo.vendor.isEmpty())
        return juce::Result::fail ("The plugin needs a company name.");

    if (! parentFolder.isDirectory() && ! parentFolder.createDirectory())
        return juce::Result::fail ("Couldn't create the folder " + parentFolder.getFullPathName());

    const auto legalName = juce::File::createLegalFileName (newInfo.name).trim();
    auto newFolder = parentFolder.getChildFile (legalName.isNotEmpty() ? legalName : juce::String ("Plugin"));

    if (newFolder.exists())
        newFolder = parentFolder.getNonexistentChildFile (newFolder.getFileName(), {}, true);

    if (! newFolder.createDirectory())
        return juce::Result::fail ("Couldn't create the folder " + newFolder.getFullPathName());

    if (newInfo.pluginId.isEmpty())
        newInfo.pluginId = makePluginId (newInfo.vendor, newInfo.name);

    newInfo.uuid = juce::Uuid().toDashedString();
    newInfo.created = juce::Time::getCurrentTime().toISO8601 (true);

    const auto previousFolder = folder;
    const auto previousInfo = info;
    const auto wasOpen = opened;

    folder = newFolder;
    info = newInfo;

    auto result = makeSubfolders();

    if (result.wasOk())
        result = write();

    if (result.failed())
    {
        newFolder.deleteRecursively();
        folder = previousFolder;
        info = previousInfo;
        opened = wasOpen;
        return result;
    }

    opened = true;
    notify();
    return juce::Result::ok();
}

juce::Result Project::open (const juce::File& projectFileOrFolder)
{
    const auto file = projectFileOrFolder.isDirectory() ? projectFileOrFolder.getChildFile (fileName)
                                                        : projectFileOrFolder;

    if (! file.existsAsFile())
        return juce::Result::fail ("There's no Stella project at " + projectFileOrFolder.getFullPathName());

    juce::var parsed;
    const auto parseResult = juce::JSON::parse (file.loadFileAsString(), parsed);

    if (parseResult.failed() || ! parsed.isObject())
        return juce::Result::fail ("The project file is damaged: " + file.getFullPathName());

    if ((int) parsed.getProperty ("format", 0) > formatVersion)
        return juce::Result::fail ("This project was made by a newer version of Stella AI Studio.");

    ProjectInfo loaded;
    loaded.name     = parsed.getProperty ("name", {}).toString().trim();
    loaded.vendor   = parsed.getProperty ("vendor", {}).toString().trim();
    loaded.kind     = kindFromString (parsed.getProperty ("kind", {}).toString());
    loaded.version  = parsed.getProperty ("version", "1.0.0").toString();
    loaded.pluginId = parsed.getProperty ("pluginId", {}).toString();
    loaded.uuid     = parsed.getProperty ("uuid", {}).toString();
    loaded.created  = parsed.getProperty ("created", {}).toString();

    if (loaded.name.isEmpty())
        loaded.name = file.getParentDirectory().getFileName();

    if (loaded.vendor.isEmpty())
        loaded.vendor = "My Company";

    // Projects copied by hand may lack their ids; they get new ones once.
    bool repaired = false;

    if (loaded.pluginId.isEmpty())
    {
        loaded.pluginId = makePluginId (loaded.vendor, loaded.name);
        repaired = true;
    }

    if (loaded.uuid.isEmpty())
    {
        loaded.uuid = juce::Uuid().toDashedString();
        repaired = true;
    }

    folder = file.getParentDirectory();
    info = loaded;
    opened = true;

    makeSubfolders();

    if (repaired)
        write();

    notify();
    return juce::Result::ok();
}

void Project::close()
{
    if (! opened)
        return;

    opened = false;
    folder = juce::File();
    info = ProjectInfo();
    notify();
}

juce::Result Project::updateInfo (const juce::String& newName, const juce::String& newVendor,
                                  PluginKind newKind, const juce::String& newVersion)
{
    if (! opened)
        return juce::Result::fail ("No project is open.");

    if (newName.trim().isEmpty() || newVendor.trim().isEmpty())
        return juce::Result::fail ("The plugin needs a name and a company name.");

    const auto previous = info;

    info.name = newName.trim();
    info.vendor = newVendor.trim();
    info.kind = newKind;
    info.version = newVersion.trim().isNotEmpty() ? newVersion.trim() : previous.version;

    const auto result = write();

    if (result.failed())
    {
        info = previous;
        return result;
    }

    notify();
    return juce::Result::ok();
}

//==============================================================================
juce::Result Project::makeSubfolders() const
{
    for (const auto& sub : { getModulesFolder(), getGuiFolder(), getAssetsFolder() })
        if (! sub.isDirectory() && ! sub.createDirectory())
            return juce::Result::fail ("Couldn't create the folder " + sub.getFullPathName());

    return juce::Result::ok();
}

juce::Result Project::write() const
{
    auto* object = new juce::DynamicObject();
    const juce::var json (object);

    object->setProperty ("format", formatVersion);
    object->setProperty ("app", juce::String ("Stella AI Studio ") + JUCE_APPLICATION_VERSION_STRING);
    object->setProperty ("name", info.name);
    object->setProperty ("vendor", info.vendor);
    object->setProperty ("kind", kindToString (info.kind));
    object->setProperty ("version", info.version);
    object->setProperty ("pluginId", info.pluginId);
    object->setProperty ("uuid", info.uuid);
    object->setProperty ("created", info.created);

    const auto file = getProjectFile();
    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (juce::JSON::toString (json))
        || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Couldn't write " + file.getFullPathName());

    return juce::Result::ok();
}

void Project::notify()
{
    if (onChanged != nullptr)
        onChanged();
}
