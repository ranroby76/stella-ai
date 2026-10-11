// C:\workspace\Stella AI Studio\src\Settings.cpp

#include "Settings.h"

namespace
{
    const juce::Identifier projectsFolderKey { "projectsFolder" };
    const juce::Identifier vendorKey         { "vendor" };
    const juce::Identifier recentKey         { "recentProjects" };
    const juce::Identifier windowKey         { "window" };
    const juce::Identifier finishSoundKey    { "finishSound" };
    const juce::Identifier stepCostsKey      { "stepCosts" };
}

//==============================================================================
Settings::Settings()
{
    data = juce::JSON::parse (getFile());

    if (! data.isObject())
        data = juce::var (new juce::DynamicObject());
}

juce::File Settings::getFile()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
               .getSiblingFile ("StellaAIStudio-settings.json");
}

juce::File Settings::getDefaultProjectsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Stella AI Studio")
               .getChildFile ("Projects");
}

void Settings::save() const
{
    juce::TemporaryFile temp (getFile());

    if (temp.getFile().replaceWithText (juce::JSON::toString (data)))
        temp.overwriteTargetFileWithTemporary();
}

//==============================================================================
juce::String Settings::getString (const juce::Identifier& key) const
{
    return data.getProperty (key, {}).toString();
}

void Settings::setString (const juce::Identifier& key, const juce::String& value)
{
    if (auto* object = data.getDynamicObject())
        object->setProperty (key, value);

    save();
}

//==============================================================================
juce::File Settings::getProjectsFolder() const
{
    const auto path = getString (projectsFolderKey);

    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        return juce::File (path);

    return getDefaultProjectsFolder();
}

void Settings::setProjectsFolder (const juce::File& folder)
{
    setString (projectsFolderKey, folder.getFullPathName());
}

juce::String Settings::getVendor() const
{
    const auto vendor = getString (vendorKey).trim();
    return vendor.isNotEmpty() ? vendor : juce::String ("My Company");
}

void Settings::setVendor (const juce::String& vendor)
{
    setString (vendorKey, vendor.trim());
}

juce::String Settings::getWindowState() const
{
    return getString (windowKey);
}

void Settings::setWindowState (const juce::String& state)
{
    setString (windowKey, state);
}

bool Settings::getFinishSound() const
{
    return (bool) data.getProperty (finishSoundKey, true);
}

void Settings::setFinishSound (bool shouldPlay)
{
    if (auto* object = data.getDynamicObject())
        object->setProperty (finishSoundKey, shouldPlay);

    save();
}

//==============================================================================
double Settings::getStepCost (const juce::String& size) const
{
    const auto record = data.getProperty (stepCostsKey, {}).getProperty (juce::Identifier (size), {});

    if ((int) record.getProperty ("count", 0) < stepCostMinimum)
        return 0.0;

    return juce::jmax (0.0, (double) record.getProperty ("average", 0.0));
}

void Settings::addStepCost (const juce::String& size, int credits)
{
    auto* object = data.getDynamicObject();

    if (object == nullptr || size.isEmpty() || credits <= 0)
        return;

    auto costs = data.getProperty (stepCostsKey, {});

    if (! costs.isObject())
    {
        costs = juce::var (new juce::DynamicObject());
        object->setProperty (stepCostsKey, costs);
    }

    // A running average over about the last steps of this size, so it follows the prices.
    const auto record = costs.getProperty (juce::Identifier (size), {});
    const auto count = (int) record.getProperty ("count", 0) + 1;
    auto average = (double) record.getProperty ("average", 0.0);
    average += ((double) credits - average) / (double) juce::jmin (count, stepCostWindow);

    auto* updated = new juce::DynamicObject();
    updated->setProperty ("count", count);
    updated->setProperty ("average", average);
    costs.getDynamicObject()->setProperty (juce::Identifier (size), juce::var (updated));

    save();
}

//==============================================================================
juce::StringArray Settings::getRecentProjects() const
{
    juce::StringArray result;

    if (const auto* list = data.getProperty (recentKey, {}).getArray())
        for (const auto& entry : *list)
            if (const auto path = entry.toString(); juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
                result.addIfNotAlreadyThere (path);

    return result;
}

void Settings::addRecentProject (const juce::File& projectFile)
{
    auto recent = getRecentProjects();
    recent.removeString (projectFile.getFullPathName());
    recent.insert (0, projectFile.getFullPathName());

    while (recent.size() > maxRecentProjects)
        recent.remove (recent.size() - 1);

    juce::Array<juce::var> list;

    for (const auto& path : recent)
        list.add (path);

    if (auto* object = data.getDynamicObject())
        object->setProperty (recentKey, list);

    save();
}

void Settings::removeRecentProject (const juce::File& projectFile)
{
    auto recent = getRecentProjects();
    recent.removeString (projectFile.getFullPathName());

    juce::Array<juce::var> list;

    for (const auto& path : recent)
        list.add (path);

    if (auto* object = data.getDynamicObject())
        object->setProperty (recentKey, list);

    save();
}
