// C:\workspace\Stella AI Studio\src\History.h

#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

//==============================================================================
/**
    Undo and redo for a project: snapshots of the files that make the plugin (graph.json,
    the modules, the GUI files, the presets), taken whenever they change: after each GUI
    edit, each preset change, and before and after each request to Stella AI. Undo puts a
    snapshot's files back on disk; the studio then rebuilds what they affect.

    The project's name, kind and ids (project.stella) aren't part of it.
*/
class History final
{
public:
    /** What an undo or redo changed, so only that gets rebuilt. */
    struct Changed
    {
        bool code = false, layout = false, schematic = false, presets = false;
        bool any() const noexcept    { return code || layout || schematic || presets; }
    };

    /** A project was opened (or closed: folder empty). Starts a fresh history. */
    void reset (const juce::File& projectFolder);

    /** Takes a snapshot if anything changed since the last one. */
    bool record (const juce::String& label);

    /** Folds changes the studio made by itself (a first automatic GUI, say) into the current
        snapshot, so they don't become a step to undo. */
    void absorb();

    bool canUndo() const noexcept   { return index > 0; }
    bool canRedo() const noexcept   { return index >= 0 && index + 1 < (int) snapshots.size(); }

    juce::String getUndoLabel() const   { return canUndo() ? snapshots[(size_t) index].label : juce::String(); }
    juce::String getRedoLabel() const   { return canRedo() ? snapshots[(size_t) index + 1].label : juce::String(); }

    Changed undo();
    Changed redo();

    static constexpr int maxSnapshots = 60;

private:
    using Files = std::map<juce::String, juce::String>;

    struct Snapshot
    {
        juce::String label;
        Files files;
    };

    Files read() const;
    Changed restore (const Files& target);

    juce::File folder;
    std::vector<Snapshot> snapshots;
    int index = -1;
};
