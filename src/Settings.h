// C:\workspace\Stella AI Studio\src\Settings.h

#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    The studio's own settings: recent projects, the projects folder, the company
    name new plugins get, and the window's position.

    Kept in one JSON file next to the executable, so a development build writes
    nothing outside its build folder.
*/
class Settings final
{
public:
    Settings();

    void save() const;

    juce::File getProjectsFolder() const;
    void setProjectsFolder (const juce::File& folder);

    juce::String getVendor() const;
    void setVendor (const juce::String& vendor);

    /** Project files, most recent first. Files that no longer exist are left out. */
    juce::StringArray getRecentProjects() const;
    void addRecentProject (const juce::File& projectFile);
    void removeRecentProject (const juce::File& projectFile);

    juce::String getWindowState() const;
    void setWindowState (const juce::String& state);

    static juce::File getFile();
    static juce::File getDefaultProjectsFolder();

    static constexpr int maxRecentProjects = 12;

private:
    juce::String getString (const juce::Identifier& key) const;
    void setString (const juce::Identifier& key, const juce::String& value);

    juce::var data;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
};
