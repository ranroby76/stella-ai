// C:\workspace\Stella AI Studio\src\Primitives.cpp

#include "Primitives.h"

#include "StellaPrimitives.h"

#include <algorithm>

//==============================================================================
GuiWidget Primitive::makeWidget() const
{
    auto json = widget.isObject() ? widget.clone() : juce::var (new juce::DynamicObject());
    json.getDynamicObject()->setProperty ("x", 0);
    json.getDynamicObject()->setProperty ("y", 0);
    return GuiLayout::widgetFromVar (json);
}

//==============================================================================
const PrimitiveLibrary& PrimitiveLibrary::get()
{
    static const PrimitiveLibrary library;
    return library;
}

PrimitiveLibrary::PrimitiveLibrary()
{
    // The prepared ones, compiled into the app...
    for (int i = 0; i < StellaPrimitives::namedResourceListSize; ++i)
    {
        const auto* resource = StellaPrimitives::namedResourceList[i];
        int size = 0;

        if (const auto* data = StellaPrimitives::getNamedResource (resource, size); data != nullptr && size > 0)
        {
            // "knob.json" -> "knob" (just the name: it's not a path on this computer).
            const auto original = juce::String (StellaPrimitives::getNamedResourceOriginalFilename (resource))
                                      .replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
            add (original.upToLastOccurrenceOf (".", false, false), juce::String::fromUTF8 (data, size));
        }
    }

    // ...then the user's own, which can replace them.
    auto files = getUserFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();

    for (const auto& file : files)
        add (file.getFileNameWithoutExtension(), file.loadFileAsString());

    std::stable_sort (primitives.begin(), primitives.end(),
                      [] (const Primitive& a, const Primitive& b) { return a.order < b.order; });
}

void PrimitiveLibrary::add (const juce::String& id, const juce::String& json)
{
    const auto parsed = juce::JSON::parse (json);

    if (! parsed.isObject() || id.isEmpty())
        return;

    Primitive p;
    p.id = id;
    p.name = parsed.getProperty ("name", id).toString();
    p.category = parsed.getProperty ("category", "More").toString();
    p.icon = parsed.getProperty ("icon", id).toString();
    p.description = parsed.getProperty ("description", {}).toString();
    p.order = (int) parsed.getProperty ("order", 1000);
    p.action = parsed.getProperty ("action", {}).toString().trim().toLowerCase();
    p.widget = parsed.getProperty ("widget", {});

    if (! p.setsBackground() && ! p.widget.isObject())
        return;   // makes nothing

    for (auto& existing : primitives)
    {
        if (existing.id == id)
        {
            existing = p;
            return;
        }
    }

    primitives.add (p);
}

juce::StringArray PrimitiveLibrary::getCategories() const
{
    juce::StringArray categories;

    for (const auto& p : primitives)
        categories.addIfNotAlreadyThere (p.category);

    return categories;
}

const Primitive* PrimitiveLibrary::find (const juce::String& id) const
{
    for (const auto& p : primitives)
        if (p.id == id)
            return &p;

    return nullptr;
}

juce::File PrimitiveLibrary::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Stella AI Studio")
               .getChildFile ("Primitives");
}

juce::String PrimitiveLibrary::idFromDrag (const juce::var& description)
{
    const auto text = description.toString();
    return text.startsWith ("primitive:") ? text.fromFirstOccurrenceOf ("primitive:", false, false) : juce::String();
}
