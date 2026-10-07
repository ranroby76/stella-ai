// C:\workspace\Stella AI Studio\src\PresetBank.h

#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

//==============================================================================
/** A named set of parameter values, by parameter id ("filter.cutoff"). */
struct Preset
{
    juce::String name;
    std::map<juce::String, float> values;
};

//==============================================================================
/**
    The plugin's presets, kept in the project as presets.json. Made in the studio (by the
    user or by Stella AI), and exported into the plugin, where a preset widget on its GUI
    steps through them.
*/
class PresetBank final
{
public:
    std::vector<Preset> presets;

    static constexpr const char* fileName = "presets.json";

    juce::Result load (const juce::File& projectFolder);
    juce::Result save (const juce::File& projectFolder) const;

    int indexOf (const juce::String& name) const;

    /** Adds the preset, or replaces the one with the same name. Returns its index. */
    int put (const Preset& preset);

    juce::StringArray getNames() const;

    static Preset fromVar (const juce::var& json);
    static juce::var toVar (const Preset& preset);
};
