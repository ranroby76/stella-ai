// C:\workspace\Stella AI Studio\src\PresetBank.cpp

#include "PresetBank.h"

Preset PresetBank::fromVar (const juce::var& json)
{
    Preset preset;
    preset.name = json.getProperty ("name", {}).toString().trim();

    if (auto* values = json.getProperty ("values", {}).getDynamicObject())
        for (const auto& entry : values->getProperties())
            if (entry.value.isDouble() || entry.value.isInt() || entry.value.isInt64())
                preset.values[entry.name.toString()] = (float) (double) entry.value;

    return preset;
}

juce::var PresetBank::toVar (const Preset& preset)
{
    auto* values = new juce::DynamicObject();

    for (const auto& [id, value] : preset.values)
        values->setProperty (id, value);

    auto* json = new juce::DynamicObject();
    json->setProperty ("name", preset.name);
    json->setProperty ("values", juce::var (values));
    return juce::var (json);
}

juce::Result PresetBank::load (const juce::File& projectFolder)
{
    presets.clear();
    const auto file = projectFolder.getChildFile (fileName);

    if (! file.existsAsFile())
        return juce::Result::ok();

    juce::var json;

    if (const auto parsed = juce::JSON::parse (file.loadFileAsString(), json); parsed.failed())
        return juce::Result::fail ("presets.json isn't valid JSON");

    if (const auto* list = json.getProperty ("presets", {}).getArray())
        for (const auto& item : *list)
            if (auto preset = fromVar (item); preset.name.isNotEmpty())
                put (preset);

    return juce::Result::ok();
}

juce::Result PresetBank::save (const juce::File& projectFolder) const
{
    juce::Array<juce::var> list;

    for (const auto& preset : presets)
        list.add (toVar (preset));

    auto* json = new juce::DynamicObject();
    json->setProperty ("format", 1);
    json->setProperty ("presets", list);

    if (! projectFolder.getChildFile (fileName).replaceWithText (juce::JSON::toString (juce::var (json))))
        return juce::Result::fail ("Couldn't save presets.json");

    return juce::Result::ok();
}

int PresetBank::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name.equalsIgnoreCase (name))
            return (int) i;

    return -1;
}

int PresetBank::put (const Preset& preset)
{
    if (const auto index = indexOf (preset.name); index >= 0)
    {
        presets[(size_t) index] = preset;
        return index;
    }

    presets.push_back (preset);
    return (int) presets.size() - 1;
}

juce::StringArray PresetBank::getNames() const
{
    juce::StringArray names;

    for (const auto& preset : presets)
        names.add (preset.name);

    return names;
}
