// C:\workspace\Stella AI Studio\src\Settings.h

#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    The studio's own settings: recent projects, the projects folder, the company
    name new plugins get, the window's position, whether a sound says Stella AI
    has finished, and what Stella AI's plan steps really cost (for its estimates).

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

    /** A short sound when Stella AI finishes a request (on unless turned off). */
    bool getFinishSound() const;
    void setFinishSound (bool shouldPlay);

    /** What a plan step of a size ("small", "medium", "large") really cost on average, in
        credits, over the last steps of that size (0 until a few were measured), and a
        finished step's cost. */
    double getStepCost (const juce::String& size) const;
    void addStepCost (const juce::String& size, int credits);
    static constexpr int stepCostMinimum = 3, stepCostWindow = 20;

    static juce::File getFile();
    static juce::File getDefaultProjectsFolder();

    static constexpr int maxRecentProjects = 12;

private:
    juce::String getString (const juce::Identifier& key) const;
    void setString (const juce::Identifier& key, const juce::String& value);

    juce::var data;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
};
