// C:\workspace\Stella AI Studio\src\Protection.cpp

#include "Protection.h"
#include "StellaRuntimeData.h"

#include <juce_cryptography/juce_cryptography.h>

#include <random>

namespace
{
    juce::String toHex (const juce::MemoryBlock& data)
    {
        return juce::String::toHexString (data.getData(), (int) data.getSize(), 0).toLowerCase();
    }

    juce::MemoryBlock fromHex (const juce::String& hex)
    {
        juce::MemoryBlock data;
        const auto clean = hex.retainCharacters ("0123456789abcdefABCDEF");

        if (clean.length() % 2 == 0)
            data.loadFromHexString (clean);

        return data;
    }

    juce::String htmlText (const juce::String& text)
    {
        return text.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
    }
}

//==============================================================================
Protection Protection::load (const juce::File& projectFolder)
{
    Protection p;
    const auto json = juce::JSON::parse (projectFolder.getChildFile (fileName));

    p.enabled = (bool) json.getProperty ("enabled", false);
    p.key = fromHex (json.getProperty ("key", {}).toString());

    if (p.key.getSize() != 32)
        p.key.reset();

    return p;
}

juce::Result Protection::save (const juce::File& projectFolder) const
{
    auto* object = new juce::DynamicObject();
    const juce::var json (object);

    object->setProperty ("enabled", enabled);
    object->setProperty ("key", toHex (key));
    object->setProperty ("note", "This plugin's serial number key. Keep a backup of it: without it, no new serials can be "
                                 "made for this plugin. Never share it.");

    const auto file = projectFolder.getChildFile (fileName);

    if (! file.replaceWithText (juce::JSON::toString (json)))
        return juce::Result::fail ("Couldn't write " + file.getFullPathName());

    return juce::Result::ok();
}

void Protection::setOn (bool shouldBeOn)
{
    enabled = shouldBeOn;

    if (shouldBeOn && ! hasKey())
        makeNewKey();
}

void Protection::makeNewKey()
{
    // 32 bytes from the system's own secure source (std::random_device: the OS's
    // cryptographic generator on Windows, /dev/urandom elsewhere).
    std::random_device source;
    key.setSize (32);

    for (size_t i = 0; i < 32; i += 4)
    {
        const auto value = source();

        for (size_t j = 0; j < 4; ++j)
            static_cast<juce::uint8*> (key.getData())[i + j] = (juce::uint8) (value >> (j * 8));
    }
}

juce::String Protection::serialFor (const juce::String& machineId) const
{
    if (! hasKey())
        return {};

    // HMAC-SHA256 (RFC 2104) over SHA-256: the key is 32 bytes, so it's padded, not hashed.
    juce::uint8 padded[64] {};
    std::memcpy (padded, key.getData(), 32);

    juce::MemoryBlock inner, outer;

    for (int i = 0; i < 64; ++i)
    {
        const auto innerByte = (juce::uint8) (padded[i] ^ 0x36), outerByte = (juce::uint8) (padded[i] ^ 0x5c);
        inner.append (&innerByte, 1);
        outer.append (&outerByte, 1);
    }

    const auto message = machineId.trim();
    inner.append (message.toRawUTF8(), message.getNumBytesAsUTF8());

    const auto innerHash = juce::SHA256 (inner).getRawData();
    outer.append (innerHash.getData(), innerHash.getSize());
    const auto mac = juce::SHA256 (outer).getRawData();

    juce::uint64 value = 0;

    for (int i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<const juce::uint8*> (mac.getData())[i];

    return juce::String ((juce::int64) (value % 1000000000ull)).paddedLeft ('0', 9);
}

juce::Result Protection::writeGenerator (const juce::File& projectFolder, const juce::String& pluginName,
                                         const juce::String& vendor, juce::File& written) const
{
    if (! hasKey())
        return juce::Result::fail ("This plugin has no serial number key yet: switch protection on first.");

    auto page = juce::String::fromUTF8 (StellaRuntimeData::serial_generator_html, StellaRuntimeData::serial_generator_htmlSize);

    if (page.startsWith ("<!--"))   // the source file's title line stays out of the page
        page = page.fromFirstOccurrenceOf ("-->", false, false).trimStart();

    page = page.replace ("__PLUGIN_NAME__", htmlText (pluginName))
               .replace ("__VENDOR__", htmlText (vendor))
               .replace ("__KEY_HEX__", toHex (key));

    written = projectFolder.getChildFile (juce::File::createLegalFileName (pluginName + " Serial Generator") + ".html");

    if (! written.replaceWithText (page))
        return juce::Result::fail ("Couldn't write " + written.getFullPathName());

    return juce::Result::ok();
}
