// C:\workspace\Stella AI Studio\src\Protection.h

#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    A project's copy protection: Fanan's registration (see stella_licence.h), switched on
    per plugin. When it's on, the exported plugin shows a Machine ID and plays in demo mode
    until the serial for that ID is typed in; the developer makes serials with the plugin's
    own serial generator page.

    Kept in <project>/protection.json: whether it's on, and the plugin's own key (32 random
    bytes, as hex). The key never leaves the developer's computer except inside the plugin
    (scrambled) and the generator page; Stella AI can't read it.
*/
class Protection final
{
public:
    static constexpr const char* fileName = "protection.json";

    /** The project's protection (off, with no key, if it was never switched on). */
    static Protection load (const juce::File& projectFolder);

    /** Writes it into the project folder. */
    juce::Result save (const juce::File& projectFolder) const;

    bool isOn() const noexcept                   { return enabled && hasKey(); }
    bool hasKey() const noexcept                 { return key.getSize() == 32; }
    const juce::MemoryBlock& getKey() const      { return key; }

    /** On (making the key the first time) or off (the key stays, so the same serials work
        again if it's switched back on). */
    void setOn (bool shouldBeOn);

    /** A new key: serials made before stop working (Save as gives the copy its own). */
    void makeNewKey();

    /** The serial generator page for this plugin: one self-contained HTML file, with the key
        in it. Written into the project folder (never beside the exported plugin, which may be
        handed out). */
    juce::Result writeGenerator (const juce::File& projectFolder, const juce::String& pluginName,
                                 const juce::String& vendor, juce::File& written) const;

    /** The nine digits for a Machine ID (as the plugin and the page compute them). */
    juce::String serialFor (const juce::String& machineId) const;

private:
    bool enabled = false;
    juce::MemoryBlock key;
};
