// C:\workspace\Stella AI Studio\src\History.cpp

#include "History.h"

namespace
{
    bool isCode (const juce::String& path)      { return path == "graph.json" || path.startsWith ("modules/"); }
}

void History::reset (const juce::File& projectFolder)
{
    folder = projectFolder;
    snapshots.clear();
    index = -1;

    if (folder != juce::File())
    {
        snapshots.push_back ({ "Opened the project", read() });
        index = 0;
    }
}

History::Files History::read() const
{
    Files files;

    auto add = [&] (const juce::File& file)
    {
        if (file.existsAsFile())
            files[file.getRelativePathFrom (folder).replaceCharacter ('\\', '/')] = file.loadFileAsString();
    };

    add (folder.getChildFile ("graph.json"));
    add (folder.getChildFile ("presets.json"));

    for (const auto& file : folder.getChildFile ("modules").findChildFiles (juce::File::findFiles, false, "*.cpp;*.h"))
        add (file);

    add (folder.getChildFile ("gui").getChildFile ("layout.json"));
    add (folder.getChildFile ("gui").getChildFile ("schematic.json"));

    // The looks the Knob Studio made for the plugin's controls.
    for (const auto& file : folder.getChildFile ("gui").getChildFile ("looks").findChildFiles (juce::File::findFiles, false, "*.fklayers"))
        add (file);

    return files;
}

bool History::record (const juce::String& label)
{
    if (folder == juce::File() || index < 0)
        return false;

    auto files = read();

    if (files == snapshots[(size_t) index].files)
        return false;

    // A new change drops what could have been redone.
    snapshots.resize ((size_t) index + 1);
    snapshots.push_back ({ label, std::move (files) });

    if ((int) snapshots.size() > maxSnapshots)
        snapshots.erase (snapshots.begin());

    index = (int) snapshots.size() - 1;
    return true;
}

void History::absorb()
{
    if (folder != juce::File() && index >= 0)
        snapshots[(size_t) index].files = read();
}

History::Changed History::restore (const Files& target)
{
    Changed changed;
    const auto current = read();

    auto mark = [&changed] (const juce::String& path)
    {
        if (isCode (path))                       changed.code = true;
        else if (path == "gui/layout.json"
                 || path.startsWith ("gui/looks/")) changed.layout = true;   // the layout is read again, and its looks
        else if (path == "gui/schematic.json")   changed.schematic = true;
        else if (path == "presets.json")         changed.presets = true;
    };

    for (const auto& [path, text] : target)
    {
        const auto found = current.find (path);

        if (found == current.end() || found->second != text)
        {
            const auto file = folder.getChildFile (path);
            file.getParentDirectory().createDirectory();
            file.replaceWithText (text);
            mark (path);
        }
    }

    // Files that didn't exist in the snapshot go.
    for (const auto& [path, text] : current)
    {
        juce::ignoreUnused (text);

        if (target.find (path) == target.end())
        {
            folder.getChildFile (path).deleteFile();
            mark (path);
        }
    }

    return changed;
}

History::Changed History::undo()
{
    if (! canUndo())
        return {};

    // Unsaved changes on disk become a step of their own first, so they can be redone.
    record ("Changes");

    if (! canUndo())
        return {};

    --index;
    return restore (snapshots[(size_t) index].files);
}

History::Changed History::redo()
{
    if (! canRedo())
        return {};

    ++index;
    return restore (snapshots[(size_t) index].files);
}
