// C:\workspace\Stella AI Studio\src\ProjectTools.cpp

#include "ProjectTools.h"
#include "GuiLayout.h"
#include "Looks.h"
#include "PresetBank.h"
#include "WasmCompiler.h"

#include "StellaRuntimeData.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    constexpr int maxFileBytes = 200 * 1024;
    constexpr int maxLogChars = 8000;
    constexpr int maxListedElements = 200;   // the GUI elements listed in the project's description

    juce::var object()
    {
        return juce::var (new juce::DynamicObject());
    }

    juce::var tool (const juce::String& name, const juce::String& description, const juce::var& properties,
                    const juce::StringArray& required)
    {
        auto schema = object();
        schema.getDynamicObject()->setProperty ("type", "object");
        schema.getDynamicObject()->setProperty ("properties", properties.isObject() ? properties : object());

        juce::Array<juce::var> needed;

        for (const auto& r : required)
            needed.add (r);

        schema.getDynamicObject()->setProperty ("required", needed);

        auto definition = object();
        definition.getDynamicObject()->setProperty ("name", name);
        definition.getDynamicObject()->setProperty ("description", description);
        definition.getDynamicObject()->setProperty ("input_schema", schema);
        return definition;
    }

    juce::var property (const juce::String& type, const juce::String& description)
    {
        auto p = object();
        p.getDynamicObject()->setProperty ("type", type);
        p.getDynamicObject()->setProperty ("description", description);
        return p;
    }

    juce::var listProperty (const juce::String& itemType, const juce::String& description)
    {
        auto items = object();
        items.getDynamicObject()->setProperty ("type", itemType);

        auto p = property ("array", description);
        p.getDynamicObject()->setProperty ("items", items);
        return p;
    }

    /** One element of the GUI in a line, its fields named as in layout.json: what the AI needs
        to find it and change it with edit_layout. */
    juce::String elementLine (int index, const GuiWidget& w, const juce::StringArray& paramIds)
    {
        juce::String line;
        line << index << ": " << GuiLayout::typeName (w.type);

        if (w.label.isNotEmpty())
            line << " \"" << w.label.replaceCharacters ("\r\n", "  ").substring (0, 48) << "\"";

        auto addParam = [&] (const juce::String& key, const juce::String& id)
        {
            line << " " << key << "=" << id;

            if (! paramIds.isEmpty() && ! paramIds.contains (id))
                line << " (no such parameter)";
        };

        if (w.param.isNotEmpty())
            addParam ("param", w.param);

        for (const auto& [role, id] : w.roles)
            addParam (role, id);

        if (w.source.isNotEmpty())
            line << " source=" << w.source;

        if (w.image.isNotEmpty())
            line << " image=" << w.image;

        if (w.type == GuiWidget::Type::keyboard)
            line << " low=" << GuiLayout::noteName (w.lowNote) << " high=" << GuiLayout::noteName (w.highNote);

        line << " x=" << w.bounds.getX() << " y=" << w.bounds.getY();

        if (w.type == GuiWidget::Type::knob)
            line << " size=" << w.bounds.getWidth();
        else
            line << " w=" << w.bounds.getWidth() << " h=" << w.bounds.getHeight();

        if (w.style.isNotEmpty())
            line << " style=\"" << w.style << "\"";

        return line;
    }

    /** An element's index as the AI gave it: a number (or a number in a string); -1 if it isn't one. */
    int indexFrom (const juce::var& value)
    {
        if (value.isInt() || value.isInt64())
            return (int) value;

        if (value.isDouble())
        {
            const auto number = (double) value;
            return std::abs (number - std::round (number)) < 1.0e-6 ? juce::roundToInt (number) : -1;
        }

        const auto text = value.toString().trim();
        return text.isNotEmpty() && text.containsOnly ("0123456789") ? text.getIntValue() : -1;
    }

    /** An element with some of its fields set anew (named as in layout.json); the rest stay. */
    GuiWidget changedWidget (const GuiWidget& widget, const juce::var& change)
    {
        auto merged = GuiLayout::widgetToVar (widget);
        auto* target = merged.getDynamicObject();
        const auto* fields = change.getDynamicObject();

        if (target == nullptr || fields == nullptr)
            return widget;

        // Fields that go by more than one name: the one given replaces them all.
        const juce::StringArray synonyms[] { { "label", "text" }, { "color", "colour" }, { "style", "look" },
                                             { "low", "from" }, { "high", "to" } };

        for (const auto& field : fields->getProperties())
            for (const auto& names : synonyms)
                if (names.contains (field.name.toString()))
                    for (const auto& name : names)
                        target->removeProperty (name);

        // A knob's diameter is its "size" (or "w").
        const auto typeAfter = fields->hasProperty ("type") ? GuiLayout::typeFromName (fields->getProperty ("type").toString()) : widget.type;

        if (typeAfter == GuiWidget::Type::knob && (fields->hasProperty ("size") || fields->hasProperty ("w")))
        {
            target->removeProperty ("size");
            target->removeProperty ("w");
        }

        for (const auto& field : fields->getProperties())
            if (field.name.toString() != "index")
                target->setProperty (field.name, field.value);

        return GuiLayout::widgetFromVar (merged);
    }

    bool isIdentifier (const juce::String& text)
    {
        return text.isNotEmpty() && text.length() <= 64
            && text.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
            && ! juce::CharacterFunctions::isDigit (text[0]);
    }

    int countLines (const juce::String& text)
    {
        return juce::StringArray::fromLines (text).size();
    }

    /** A picture's size in pixels, read once per version of the file. */
    juce::Point<int> pictureSize (const juce::File& file)
    {
        static std::map<juce::String, std::pair<juce::Time, juce::Point<int>>> known;

        const auto modified = file.getLastModificationTime();
        auto& entry = known[file.getFullPathName()];

        if (entry.first != modified)
        {
            const auto image = juce::ImageFileFormat::loadFrom (file);
            entry = { modified, { image.getWidth(), image.getHeight() } };
        }

        return entry.second;
    }
}

//==============================================================================
ProjectTools::ProjectTools (Project& p, LivePreview& lp)
    : project (p), preview (lp)
{
}

const juce::String& ProjectTools::getGuide()
{
    static const juce::String guide = [] {
        juce::String text = juce::String::fromUTF8 (StellaRuntimeData::stella_builder_guide_md, StellaRuntimeData::stella_builder_guide_mdSize);
        text << "\n\n## stella_api.h\n```cpp\n"
             << juce::String::fromUTF8 (StellaRuntimeData::stella_api_h, StellaRuntimeData::stella_api_hSize)
             << "\n```\n";
        return text;
    }();

    return guide;
}

//==============================================================================
juce::var ProjectTools::getDefinitions (bool builder) const
{
    juce::Array<juce::var> tools;

    tools.add (tool ("read_project",
                     "Shows the open project: its name and type, graph.json, its files, and the last build's result.",
                     object(), {}));

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("path", property ("string", "graph.json, gui/layout.json, or a file in modules/ such as modules/LowPass.cpp"));
        tools.add (tool ("read_file", "Reads one of the project's files.", props, { "path" }));
    }

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("layout", property ("object", "The whole GUI: { \"format\": 1, \"width\", \"height\", \"background\", \"widgets\": [...] }"));
        tools.add (tool ("set_layout",
                         "Replaces the whole GUI (gui/layout.json), for a new design. It shows at once; no build needed. "
                         "To change part of it, use edit_layout instead. If you do replace it, read gui/layout.json just before, "
                         "to keep what the user arranged by hand.",
                         props, { "layout" }));
    }

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("remove", listProperty ("integer", "Indexes of elements to delete."));
        props.getDynamicObject()->setProperty ("change", listProperty ("object", "Elements to change: { \"index\": 3, then the fields to set, named as in layout.json, "
                                                                                  "e.g. \"x\": 40, \"label\": \"Cutoff\", \"style\": \"Black knob\" }. Fields not given stay as they are."));
        props.getDynamicObject()->setProperty ("add", listProperty ("object", "New elements, written as in layout.json, e.g. { \"type\": \"keyboard\", \"x\": 20, \"y\": 330, "
                                                                               "\"w\": 720, \"h\": 80, \"low\": \"C2\", \"high\": \"C6\" }. They go in front of the others; "
                                                                               "\"at\": 0 puts one at the back."));
        props.getDynamicObject()->setProperty ("width", property ("integer", "The window's new width."));
        props.getDynamicObject()->setProperty ("height", property ("integer", "The window's new height."));
        props.getDynamicObject()->setProperty ("background", property ("object", "The window's colours: { \"top\": \"#FF2B2B30\", \"bottom\": \"#FF17171A\" }."));
        tools.add (tool ("edit_layout",
                         "Changes part of the plugin's GUI and keeps everything else exactly as it is: removes, changes or adds elements, "
                         "or resizes the window. Elements are named by their index in the project's \"GUI elements\" list; one call's "
                         "changes and removals all use that list, then its additions go in. Put all of a step's GUI edits in one call: the "
                         "indexes change after it. The window grows by itself to fit what's added or moved. It shows at once; no build needed.",
                         props, {}));
    }

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("name", property ("string", "The preset's name, e.g. \"Warm Pad\". A preset with that name is replaced."));
        props.getDynamicObject()->setProperty ("values", property ("object", "Parameter values by id, e.g. { \"filter.cutoff\": 800 }. Parameters left out keep their defaults."));
        tools.add (tool ("save_preset",
                         "Saves a named preset: a set of parameter values the user picks from the preset list, also in the exported plugin.",
                         props, { "name", "values" }));
    }

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("name", property ("string", "A short name for the plugin, from the request, e.g. \"Ice Synth\"."));
        props.getDynamicObject()->setProperty ("kind", property ("string", "\"instrument\" (notes in, sound out) or \"effect\" (audio in, audio out)."));
        tools.add (tool ("create_project",
                         "Creates a new plugin project and opens it, ready to build. Use it when no project is open and the user wants a "
                         "plugin, or when the user asks for a new plugin. Don't ask the user to do it.",
                         props, { "name", "kind" }));
    }

    {
        // Handled by the conversation itself (StellaAi): a checklist the user watches.
        auto props = object();
        props.getDynamicObject()->setProperty ("steps", listProperty ("string", "The steps in order, a few plain words each in the user's language (no file or code names), "
                                                                                 "e.g. [\"New filter types\", \"Filter sound\", \"Try it out\", \"Panel with drop-down lists\"]. "
                                                                                 "Giving steps starts a new plan."));
        props.getDynamicObject()->setProperty ("current", property ("integer", "The step starting now, counting from 1."));
        props.getDynamicObject()->setProperty ("done", property ("boolean", "true when the last step is finished."));
        tools.add (tool ("plan",
                         "Shows the user your steps for this request as a checklist that ticks along. For any request that takes more "
                         "than one step, call it first with all the steps (small ones: one file per step, then build, then the panel) "
                         "and current 1; then with current as each next step starts (in the same answer as that step's own tool calls); "
                         "then with done: true. The project's description shows an unfinished plan: carry on from it.",
                         props, {}));
    }

    {
        // In both lists: a conversation hands over with it, and the builder must still
        // recognise that step in the request it takes over.
        auto props = object();
        props.getDynamicObject()->setProperty ("task", property ("string", "What to build or change, in a few sentences."));
        tools.add (tool ("start_building",
                         builder ? "Already building: there's no need to call this."
                                 : "Hands over to Stella's builder to create or change the plugin's sound: anything that needs code written or "
                                   "changed, or the graph rewired. Call it as soon as the user asks for a plugin or a change to one.",
                         props, { "task" }));
    }

    if (! builder)
        return tools;

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("path", property ("string", "modules/<Name>.cpp for a module, modules/<Name>.h for a shared helper"));
        props.getDynamicObject()->setProperty ("content", property ("string", "The complete file."));
        tools.add (tool ("write_file", "Creates or replaces a file in modules/. Always give the complete file.", props, { "path", "content" }));
    }

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("path", property ("string", "A file in modules/"));
        tools.add (tool ("delete_file", "Deletes a file in modules/ that the plugin no longer uses.", props, { "path" }));
    }

    {
        auto props = object();
        props.getDynamicObject()->setProperty ("graph", property ("object", "The whole graph: { \"format\": 1, \"modules\": [...], \"wires\": [...] }"));
        tools.add (tool ("set_graph", "Replaces graph.json: which modules the plugin has and how they're wired.", props, { "graph" }));
    }

    tools.add (tool ("build",
                     "Compiles the project and plays it live in the studio. Returns the plugin's parameters, or the compiler's errors to fix.",
                     object(), {}));

    return tools;
}

//==============================================================================
juce::String ProjectTools::describeProject() const
{
    if (! project.isOpen())
        return "No project is open. If the user wants a plugin, create one with create_project (a short name from the request, "
               "the right kind), then build it. Don't ask the user to create it.";

    const auto& info = project.getInfo();
    juce::String text;

    text << "Project \"" << info.name << "\" (" << Project::kindDisplayName (info.kind) << ") by " << info.vendor << ".\n";

    if (info.kind == PluginKind::effect)
        text << "It's an audio effect: it reads plugin.in L / plugin.in R and writes plugin.out L / plugin.out R.\n";
    else if (info.kind == PluginKind::noteEffect)
        text << "It's meant as a MIDI effect, but MIDI output isn't available yet: for now build it as an instrument.\n";
    else
        text << "It's an instrument: notes in, audio out on plugin.out L / plugin.out R.\n";

    const auto graph = project.getFolder().getChildFile (WasmCompiler::graphFileName);
    text << "\ngraph.json:\n" << (graph.existsAsFile() ? graph.loadFileAsString().trim() : juce::String ("(none yet)")) << "\n";

    auto files = project.getModulesFolder().findChildFiles (juce::File::findFiles, false, "*.cpp;*.h");
    files.sort();

    text << "\nFiles in modules/:\n";

    if (files.isEmpty())
        text << "(none)\n";

    for (const auto& f : files)
        text << "- modules/" << f.getFileName() << " (" << countLines (f.loadFileAsString()) << " lines)\n";

    const auto layoutFile = project.getGuiFolder().getChildFile (GuiLayout::fileName);
    GuiLayout layout;

    if (GuiLayout::load (layoutFile, layout).wasOk())
    {
        text << "\nGUI: gui/layout.json, " << layout.width << " x " << layout.height << ", " << (int) layout.widgets.size() << " elements"
             << (layout.backgroundPicture().isNotEmpty() ? ", background picture " + layout.backgroundPicture() : juce::String()) << ".\n";

        // Every element, by index: edit_layout changes them without the whole file going back and forth.
        if (! layout.widgets.empty())
        {
            juce::StringArray paramIds;

            for (const auto& p : preview.getParameters())
                paramIds.add (p.id);

            text << "GUI elements, back to front (edit_layout names them by these indexes):\n";

            const auto listed = juce::jmin ((int) layout.widgets.size(), maxListedElements);

            for (int i = 0; i < listed; ++i)
                text << elementLine (i, layout.widgets[(size_t) i], paramIds) << "\n";

            if (listed < (int) layout.widgets.size())
                text << "... and " << ((int) layout.widgets.size() - listed) << " more (read gui/layout.json for all of them).\n";
        }
    }
    else
    {
        text << "\nGUI: none yet (the studio makes a plain automatic one after the first build).\n";
    }

    // The pictures the user added, for backgrounds and picture elements.
    auto pictures = project.getGuiFolder().getChildFile (GuiLayout::imagesFolder)
                        .findChildFiles (juce::File::findFiles, false, GuiLayout::pictureFiles());
    pictures.sort();

    if (! pictures.isEmpty())
    {
        text << "\nPictures in gui/images (use them by file name):\n";

        for (const auto& f : pictures)
        {
            const auto size = pictureSize (f);
            text << "- " << f.getFileName() << " (" << size.x << " x " << size.y << ")\n";
        }
    }

    // The looks knobs, sliders and switches can wear.
    {
        Looks looks;
        looks.setGuiFolder (project.getGuiFolder());

        text << "\nLooks for knobs, sliders and switches (a control's \"style\"; without one it wears its kind's default):\n";

        for (const auto kind : { Looks::Kind::knob, Looks::Kind::slider, Looks::Kind::sliderAcross, Looks::Kind::button })
        {
            juce::StringArray entries;

            for (const auto* look : looks.listFor (kind))
                entries.add (look->name
                             + (look->description.isNotEmpty() ? " (" + look->description + ")" : juce::String())
                             + (look->origin == Looks::Origin::project ? juce::String (" [this plugin's own]")
                                : look->origin == Looks::Origin::mine  ? juce::String (" [the user's]") : juce::String()));

            text << "- " << (kind == Looks::Kind::knob         ? "knobs"
                           : kind == Looks::Kind::slider       ? "sliders"
                           : kind == Looks::Kind::sliderAcross ? "sliders across, \"orientation\": \"horizontal\""
                                                               : "switches")
                 << " (default " << looks.defaultFor (kind).name << "): " << entries.joinIntoString ("; ") << "\n";
        }
    }

    PresetBank bank;
    bank.load (project.getFolder());

    if (! bank.presets.empty())
        text << "\nPresets: " << bank.getNames().joinIntoString (", ") << "\n";

    if (! preview.getParameters().isEmpty())
    {
        text << "\nParameters (bind GUI controls to these ids):\n";

        for (const auto& p : preview.getParameters())
            text << "- " << p.id << " \"" << p.name << "\" " << juce::String (p.min, 3) << ".." << juce::String (p.max, 3)
                 << (p.unit.isNotEmpty() ? " " + p.unit : juce::String()) << "\n";
    }

    if (! preview.getModules().isEmpty())
    {
        juce::StringArray signals { "plugin.out L", "plugin.out R" };

        if (info.kind == PluginKind::effect)
            signals.addArray ({ "plugin.in L", "plugin.in R" });

        for (const auto& m : preview.getModules())
            for (const auto& out : m.outputs)
                signals.add (m.id + "." + out);

        text << "\nSignals meters and scopes can watch: " << signals.joinIntoString (", ") << "\n";

        juce::StringArray displays;

        for (const auto& d : preview.getDisplays())
            displays.add (d.id);

        if (! displays.isEmpty())
            text << "Module displays (0..1 for meters, or lamps): " << displays.joinIntoString (", ") << "\n";
    }

    text << "\nLast build: ";

    switch (preview.getState())
    {
        case LivePreview::State::idle:      text << "none yet.\n"; break;
        case LivePreview::State::building:  text << "building now.\n"; break;
        case LivePreview::State::playing:   text << "playing live (" << preview.getStatus() << ").\n"; break;
        case LivePreview::State::failed:    text << "failed: " << preview.getStatus() << "\n" << preview.getLog().substring (0, 1500) << "\n"; break;
    }

    return text;
}

//==============================================================================
juce::Result ProjectTools::resolve (const juce::String& path, juce::File& file, bool forWriting) const
{
    if (! project.isOpen())
        return juce::Result::fail ("No project is open: the user has to create one (New) or open one first.");

    const auto clean = path.trim().replaceCharacter ('\\', '/');

    if (clean == WasmCompiler::graphFileName && ! forWriting)
    {
        file = project.getFolder().getChildFile (WasmCompiler::graphFileName);
        return juce::Result::ok();
    }

    if (clean == juce::String ("gui/") + GuiLayout::fileName && ! forWriting)
    {
        file = project.getGuiFolder().getChildFile (GuiLayout::fileName);
        return juce::Result::ok();
    }

    // Only modules/<identifier>.cpp or .h: nothing outside the project's modules folder.
    if (clean.startsWith ("modules/"))
    {
        const auto name = clean.fromFirstOccurrenceOf ("modules/", false, false);
        const auto stem = name.upToLastOccurrenceOf (".", false, false);
        const auto extension = name.fromLastOccurrenceOf (".", false, false);

        if (isIdentifier (stem) && (extension == "cpp" || extension == "h"))
        {
            file = project.getModulesFolder().getChildFile (name);
            return juce::Result::ok();
        }
    }

    return juce::Result::fail ("\"" + path + "\" isn't allowed: use graph.json or gui/layout.json (read only; write them with "
                               "set_graph and set_layout) or modules/<Name>.cpp / modules/<Name>.h.");
}

juce::String ProjectTools::describeCall (const juce::String& name, const juce::var& input)
{
    // In plain words: the user sees what's happening, not how it's made.
    const auto path = input.getProperty ("path", {}).toString();

    if (name == "write_file")
    {
        // modules/StereoWidener.cpp: "Making the stereo widener". Headers go by quietly.
        if (! path.startsWith ("modules/") || path.endsWithIgnoreCase (".h"))
            return {};

        const auto stem = path.fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
        juce::String words;

        for (int i = 0; i < stem.length(); ++i)
        {
            const auto c = stem[i];

            if (c == '_' || c == '-')
            {
                words << ' ';
                continue;
            }

            if (i > 0 && juce::CharacterFunctions::isUpperCase (c) && ! juce::CharacterFunctions::isUpperCase (stem[i - 1]))
                words << ' ';

            words << juce::String::charToString (juce::CharacterFunctions::toLowerCase (c));
        }

        return "Making the " + words.trim();
    }

    if (name == "delete_file")
        return "Tidying up";

    if (name == "set_graph")
        return "Connecting everything";

    if (name == "build")
        return juce::String::fromUTF8 ("Building\xe2\x80\xa6");

    if (name == "read_project")
        return "Looking at your plugin";

    if (name == "save_preset")
        return "Saving the preset \"" + input.getProperty ("name", {}).toString() + "\"";

    if (name == "create_project")
        return "Creating the project \"" + input.getProperty ("name", {}).toString() + "\"";

    if (name == "set_layout" || name == "edit_layout")
        return "Designing the panel";

    return {};
}

juce::String ProjectTools::buildResult (bool ok) const
{
    if (! ok)
    {
        auto log = preview.getLog();

        if (log.length() > maxLogChars)
            log = log.substring (0, maxLogChars) + "\n[... more errors cut]";

        return preview.getStatus() + "\n" + log;
    }

    juce::String text;
    text << preview.getStatus() << ". It's playing live now.\n";

    const auto& params = preview.getParameters();
    text << "Parameters (" << params.size() << "):\n";

    for (const auto& p : params)
        text << "- " << p.id << " \"" << p.name << "\" " << juce::String (p.min, 3) << ".." << juce::String (p.max, 3)
             << (p.unit.isNotEmpty() ? " " + p.unit : juce::String()) << ", default " << juce::String (p.def, 3) << "\n";

    if (preview.getLog().isNotEmpty())
        text << "\nCompiler warnings:\n" << preview.getLog().substring (0, 3000);

    return text;
}

juce::String ProjectTools::checkLayout (const GuiLayout& layout, const GuiLayout& before) const
{
    juce::StringArray unbound;

    for (const auto& w : layout.widgets)
    {
        juce::StringArray ids;

        if (w.param.isNotEmpty())
            ids.add (w.param);

        for (const auto& [role, id] : w.roles)
            if (id.isNotEmpty())
                ids.add (id);

        for (const auto& id : ids)
        {
            bool known = false;

            for (const auto& p : preview.getParameters())
                known = known || p.id == id;

            if (! known)
                unbound.addIfNotAlreadyThere (id);
        }
    }

    // Pictures must be files the user added.
    juce::StringArray missingPictures;
    const auto pictures = project.getGuiFolder().getChildFile (GuiLayout::imagesFolder);

    for (const auto& w : layout.widgets)
        if (w.type == GuiWidget::Type::image && w.image.isNotEmpty() && ! pictures.getChildFile (w.image).existsAsFile())
            missingPictures.addIfNotAlreadyThere (w.image);

    // Looks must exist, for that kind of control (an old name that was already there isn't Stella's to answer for).
    juce::StringArray unknownLooks;

    {
        Looks looks;
        looks.setGuiFolder (project.getGuiFolder());

        juce::StringArray woreBefore;

        for (const auto& w : before.widgets)
            if (Looks::takesLook (w))
                woreBefore.addIfNotAlreadyThere (Looks::kindName (Looks::kindOf (w)) + "|" + w.style);

        for (const auto& w : layout.widgets)
            if (Looks::takesLook (w) && w.style.isNotEmpty() && ! woreBefore.contains (Looks::kindName (Looks::kindOf (w)) + "|" + w.style))
                if (const auto* look = looks.find (w.style); look == nullptr || look->kind != Looks::kindOf (w))
                    unknownLooks.addIfNotAlreadyThere (w.style);
    }

    return (unbound.isEmpty() || preview.getParameters().isEmpty() ? juce::String()
                                                                  : "\nThese parameter ids don't exist in the plugin (fix or remove those elements): "
                                                                        + unbound.joinIntoString (", "))
         + (missingPictures.isEmpty() ? juce::String()
                                      : "\nThese pictures aren't in gui/images (use only the listed ones): " + missingPictures.joinIntoString (", "))
         + (unknownLooks.isEmpty() ? juce::String()
                                   : "\nThese looks don't exist for those controls, so they show a default (use only the listed looks, of the right kind): "
                                         + unknownLooks.joinIntoString (", "));
}

//==============================================================================
void ProjectTools::run (const juce::String& name, const juce::var& input, Done done)
{
    auto fail = [&done] (const juce::String& message) { done (message, true); };

    if (name == "read_project")
    {
        done (describeProject(), false);
        return;
    }

    if (name == "read_file")
    {
        juce::File file;

        if (const auto r = resolve (input.getProperty ("path", {}).toString(), file, false); r.failed())
            return fail (r.getErrorMessage());

        if (! file.existsAsFile())
            return fail ("There's no file " + input.getProperty ("path", {}).toString() + ".");

        done (file.loadFileAsString(), false);
        return;
    }

    if (name == "write_file")
    {
        juce::File file;

        if (const auto r = resolve (input.getProperty ("path", {}).toString(), file, true); r.failed())
            return fail (r.getErrorMessage());

        const auto content = input.getProperty ("content", {}).toString();

        if (content.getNumBytesAsUTF8() > (size_t) maxFileBytes)
            return fail ("That file is too big (over 200 KB). Split it into smaller modules.");

        file.getParentDirectory().createDirectory();

        if (! file.replaceWithText (content))
            return fail ("Couldn't write " + file.getFullPathName());

        done ("Wrote modules/" + file.getFileName() + " (" + juce::String (countLines (content)) + " lines).", false);
        return;
    }

    if (name == "delete_file")
    {
        juce::File file;

        if (const auto r = resolve (input.getProperty ("path", {}).toString(), file, true); r.failed())
            return fail (r.getErrorMessage());

        if (file.existsAsFile() && ! file.deleteFile())
            return fail ("Couldn't delete " + file.getFullPathName());

        done ("Deleted modules/" + file.getFileName() + ".", false);
        return;
    }

    if (name == "set_graph")
    {
        if (! project.isOpen())
            return fail ("No project is open: the user has to create one (New) or open one first.");

        const auto graph = input.getProperty ("graph", {});

        if (! graph.isObject() || graph.getProperty ("modules", {}).getArray() == nullptr)
            return fail ("The graph needs a \"modules\" list (and usually \"wires\").");

        const auto file = project.getFolder().getChildFile (WasmCompiler::graphFileName);

        if (! file.replaceWithText (juce::JSON::toString (graph)))
            return fail ("Couldn't write graph.json");

        const auto* modules = graph.getProperty ("modules", {}).getArray();
        const auto* wires = graph.getProperty ("wires", {}).getArray();
        done ("graph.json saved: " + juce::String (modules->size()) + " modules, "
              + juce::String (wires != nullptr ? wires->size() : 0) + " wires. Build to hear it.", false);
        return;
    }

    if (name == "set_layout")
    {
        if (! project.isOpen())
            return fail ("No project is open: the user has to create one (New) or open one first.");

        const auto json = input.getProperty ("layout", {});

        if (! json.isObject() || json.getProperty ("widgets", {}).getArray() == nullptr)
            return fail ("The layout needs a \"widgets\" list (and usually width, height and background).");

        // Read through the layout model, so what's saved is always valid and tidy.
        const auto layout = GuiLayout::fromVar (json);
        const auto file = project.getGuiFolder().getChildFile (GuiLayout::fileName);

        // What it wore before: an old name that was already there isn't Stella's to answer for.
        GuiLayout before;
        GuiLayout::load (file, before);

        if (const auto saved = layout.save (file); saved.failed())
            return fail (saved.getErrorMessage());

        if (onLayoutChanged != nullptr)
            onLayoutChanged();

        done ("The GUI is showing: " + juce::String ((int) layout.widgets.size()) + " elements, " + juce::String (layout.width)
                  + " x " + juce::String (layout.height) + "." + checkLayout (layout, before),
              false);
        return;
    }

    if (name == "edit_layout")
    {
        if (! project.isOpen())
            return fail ("No project is open: the user has to create one (New) or open one first.");

        const auto file = project.getGuiFolder().getChildFile (GuiLayout::fileName);
        GuiLayout layout;

        if (! file.existsAsFile() || GuiLayout::load (file, layout).failed())
            return fail ("There's no GUI yet: build first (the studio then makes a plain one), or design one with set_layout.");

        const auto before = layout;
        const auto count = (int) layout.widgets.size();
        const juce::Rectangle<int> oldWindow (0, 0, layout.width, layout.height);
        juce::StringArray problems;
        std::vector<juce::Rectangle<int>> placed;   // where changed and added elements went, for the window to fit
        int changedCount = 0, addedCount = 0;

        // Changes first, then removals, both by the indexes as listed.
        if (const auto* changes = input.getProperty ("change", {}).getArray())
        {
            for (const auto& change : *changes)
            {
                const auto index = change.hasProperty ("index") ? indexFrom (change.getProperty ("index", {})) : -1;

                if (! juce::isPositiveAndBelow (index, count) || change.getDynamicObject() == nullptr)
                {
                    problems.add ("there's no element " + change.getProperty ("index", "?").toString() + " to change");
                    continue;
                }

                auto& w = layout.widgets[(size_t) index];
                w = changedWidget (w, change);
                placed.push_back (w.bounds);
                ++changedCount;
            }
        }

        std::vector<int> removals;

        if (const auto* list = input.getProperty ("remove", {}).getArray())
        {
            for (const auto& item : *list)
            {
                const auto index = indexFrom (item);

                if (juce::isPositiveAndBelow (index, count))
                    removals.push_back (index);
                else
                    problems.add ("there's no element " + item.toString() + " to remove");
            }
        }

        std::sort (removals.begin(), removals.end());
        removals.erase (std::unique (removals.begin(), removals.end()), removals.end());

        for (auto it = removals.rbegin(); it != removals.rend(); ++it)
            layout.widgets.erase (layout.widgets.begin() + *it);

        // Then additions: in front, or at "at".
        if (const auto* additions = input.getProperty ("add", {}).getArray())
        {
            for (const auto& item : *additions)
            {
                if (item.getDynamicObject() == nullptr || ! item.hasProperty ("type"))
                {
                    problems.add ("an addition had no \"type\"");
                    continue;
                }

                const auto w = GuiLayout::widgetFromVar (item);
                const auto at = item.hasProperty ("at") ? juce::jlimit (0, (int) layout.widgets.size(), (int) item.getProperty ("at", 0))
                                                        : (int) layout.widgets.size();
                layout.widgets.insert (layout.widgets.begin() + at, w);
                placed.push_back (w.bounds);
                ++addedCount;
            }
        }

        // The window: as asked, then big enough for what was placed; a picture filling it keeps filling it.
        if (input.hasProperty ("width"))
            layout.width = juce::jlimit (GuiLayout::minWidth, GuiLayout::maxWidth, (int) input.getProperty ("width", layout.width));

        if (input.hasProperty ("height"))
            layout.height = juce::jlimit (GuiLayout::minHeight, GuiLayout::maxHeight, (int) input.getProperty ("height", layout.height));

        if (const auto colours = input.getProperty ("background", {}); colours.isObject())
        {
            layout.backgroundTop = GuiLayout::colourFromString (colours.getProperty ("top", {}).toString(), layout.backgroundTop);
            layout.backgroundBottom = GuiLayout::colourFromString (colours.getProperty ("bottom", {}).toString(), layout.backgroundBottom);
        }

        const auto askedWidth = layout.width, askedHeight = layout.height;

        for (const auto& bounds : placed)
        {
            if (bounds == oldWindow)
                continue;   // a picture filling the window

            layout.width = juce::jlimit (GuiLayout::minWidth, GuiLayout::maxWidth, juce::jmax (layout.width, bounds.getRight()));
            layout.height = juce::jlimit (GuiLayout::minHeight, GuiLayout::maxHeight, juce::jmax (layout.height, bounds.getBottom()));
        }

        if (layout.width != oldWindow.getWidth() || layout.height != oldWindow.getHeight())
            for (auto& w : layout.widgets)
                if (w.type == GuiWidget::Type::image && w.bounds == oldWindow)
                    w.bounds = { 0, 0, layout.width, layout.height };

        if (const auto saved = layout.save (file); saved.failed())
            return fail (saved.getErrorMessage());

        if (onLayoutChanged != nullptr)
            onLayoutChanged();

        juce::StringArray what;

        if (changedCount > 0)          what.add ("changed " + juce::String (changedCount));
        if (! removals.empty())        what.add ("removed " + juce::String ((int) removals.size()));
        if (addedCount > 0)            what.add ("added " + juce::String (addedCount));

        done ("The GUI is showing: " + juce::String ((int) layout.widgets.size()) + " elements, " + juce::String (layout.width) + " x "
                  + juce::String (layout.height) + (what.isEmpty() ? juce::String (".") : " (" + what.joinIntoString (", ") + ").")
                  + (layout.width != askedWidth || layout.height != askedHeight ? "\nThe window grew to fit what was placed." : juce::String())
                  + (problems.isEmpty() ? juce::String() : "\nNot done: " + problems.joinIntoString ("; ") + ".")
                  + checkLayout (layout, before)
                  + (what.isEmpty() ? juce::String() : "\nThe indexes have changed: use the project's updated \"GUI elements\" list for further edits."),
              ! problems.isEmpty() && what.isEmpty());
        return;
    }

    if (name == "create_project")
    {
        const auto projectName = input.getProperty ("name", {}).toString().trim().substring (0, 60);
        const auto kind = input.getProperty ("kind", {}).toString().trim().toLowerCase() == "effect" ? PluginKind::effect : PluginKind::instrument;

        if (projectName.isEmpty())
            return fail ("The project needs a name.");

        if (onCreateProject == nullptr)
            return fail ("Projects can't be created from here.");

        if (const auto created = onCreateProject (projectName, kind); created.failed())
            return fail (created.getErrorMessage());

        done ("Created and opened the project \"" + projectName + "\" (" + Project::kindDisplayName (kind)
                  + "). It's empty: write its modules, wire them with set_graph, then build.\n\n"
                  + describeProject(),
              false);
        return;
    }

    if (name == "save_preset")
    {
        if (! project.isOpen())
            return fail ("No project is open: the user has to create one (New) or open one first.");

        Preset preset;
        preset.name = input.getProperty ("name", {}).toString().trim().substring (0, 60);

        if (preset.name.isEmpty())
            return fail ("The preset needs a name.");

        // Every parameter gets a value: the ones given (kept in range), the rest their defaults.
        const auto* given = input.getProperty ("values", {}).getDynamicObject();
        juce::StringArray unknown;

        for (const auto& p : preview.getParameters())
        {
            auto value = p.def;

            if (given != nullptr && given->hasProperty (p.id))
                value = juce::jlimit (p.min, p.max, (float) (double) given->getProperty (p.id));

            preset.values[p.id] = value;
        }

        if (given != nullptr)
            for (const auto& entry : given->getProperties())
                if (preset.values.find (entry.name.toString()) == preset.values.end())
                    unknown.add (entry.name.toString());

        PresetBank bank;
        bank.load (project.getFolder());
        bank.put (preset);

        if (const auto saved = bank.save (project.getFolder()); saved.failed())
            return fail (saved.getErrorMessage());

        if (onPresetsChanged != nullptr)
            onPresetsChanged();

        done ("Saved the preset \"" + preset.name + "\" (" + juce::String ((int) bank.presets.size()) + " presets now)."
                  + (unknown.isEmpty() ? juce::String() : "\nThese ids don't exist and were left out: " + unknown.joinIntoString (", ")),
              false);
        return;
    }

    if (name == "build")
    {
        if (! project.isOpen())
            return fail ("No project is open: the user has to create one (New) or open one first.");

        preview.build (project.getFolder(), project.getInfo().uuid, [this, done] (bool ok)
        {
            done (buildResult (ok), ! ok);
        });
        return;
    }

    fail ("There's no tool called " + name + ".");
}
