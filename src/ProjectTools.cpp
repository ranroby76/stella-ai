// C:\workspace\Stella AI Studio\src\ProjectTools.cpp

#include "ProjectTools.h"
#include "GuiLayout.h"
#include "PresetBank.h"
#include "WasmCompiler.h"

#include "StellaRuntimeData.h"

#include <map>

namespace
{
    constexpr int maxFileBytes = 200 * 1024;
    constexpr int maxLogChars = 8000;

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
        props.getDynamicObject()->setProperty ("layout", property ("object", "The whole GUI: { \"format\": 1, \"width\", \"height\", \"background\", \"styles\", \"widgets\": [...] }"));
        tools.add (tool ("set_layout",
                         "Replaces the plugin's GUI (gui/layout.json). It shows at once; no build needed. "
                         "Read gui/layout.json first and edit it, to keep what the user arranged by hand.",
                         props, { "layout" }));
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
        text << "\nGUI: gui/layout.json, " << layout.width << " x " << layout.height << ", " << (int) layout.widgets.size() << " elements"
             << (layout.backgroundImage.isNotEmpty() ? ", background picture " + layout.backgroundImage : juce::String()) << ".\n";
    else
        text << "\nGUI: none yet (the studio makes a plain automatic one after the first build).\n";

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

    if (name == "set_layout")
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
            return fail ("The layout needs a \"widgets\" list (and usually width, height, background and styles).");

        // Read through the layout model, so what's saved is always valid and tidy.
        const auto layout = GuiLayout::fromVar (json);
        const auto file = project.getGuiFolder().getChildFile (GuiLayout::fileName);

        if (const auto saved = layout.save (file); saved.failed())
            return fail (saved.getErrorMessage());

        juce::StringArray unbound;

        for (const auto& w : layout.widgets)
        {
            if (w.param.isEmpty())
                continue;

            bool known = false;

            for (const auto& p : preview.getParameters())
                known = known || p.id == w.param;

            if (! known)
                unbound.addIfNotAlreadyThere (w.param);
        }

        // Pictures must be files the user added.
        juce::StringArray missingPictures;
        const auto pictures = project.getGuiFolder().getChildFile (GuiLayout::imagesFolder);

        if (layout.backgroundImage.isNotEmpty() && ! pictures.getChildFile (layout.backgroundImage).existsAsFile())
            missingPictures.add (layout.backgroundImage);

        for (const auto& w : layout.widgets)
            if (w.type == GuiWidget::Type::image && w.image.isNotEmpty() && ! pictures.getChildFile (w.image).existsAsFile())
                missingPictures.addIfNotAlreadyThere (w.image);

        if (onLayoutChanged != nullptr)
            onLayoutChanged();

        done ("The GUI is showing: " + juce::String ((int) layout.widgets.size()) + " elements, " + juce::String (layout.width)
                  + " x " + juce::String (layout.height) + "."
                  + (unbound.isEmpty() ? juce::String() : "\nThese parameter ids don't exist in the plugin (fix them): " + unbound.joinIntoString (", "))
                  + (missingPictures.isEmpty() ? juce::String()
                                               : "\nThese pictures aren't in gui/images (use only the listed ones): " + missingPictures.joinIntoString (", ")),
              false);
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
