// C:\workspace\Stella AI Studio\src\CloudProjects.h

#pragma once

#include <juce_events/juce_events.h>

#include "FananServer.h"
#include "Project.h"

#include <functional>

//==============================================================================
/**
    Projects kept in the cloud (Fanan's server), for signed-in users: the open project is
    saved there by itself a few seconds after it changes, and any project can be opened
    from the cloud on another computer.

    What goes up is the project's text: project.stella, graph.json, the modules and the GUI
    files (at most 2 MB). Each save carries the version it started from, so a project
    changed elsewhere in the meantime isn't overwritten: the user chooses which copy wins.

    A small file in the project folder (.stella-cloud.json) remembers the last version.
*/
class CloudProjects final : private juce::Timer
{
public:
    struct Entry
    {
        juce::String uuid, name, kind, updated;
        int version = 0, size = 0;
    };

    enum class State { off, waiting, saving, saved, failed, conflict };

    CloudProjects (FananServer& server, Project& project);
    ~CloudProjects() override;

    /** A project was opened or closed: start tracking it. */
    void projectChanged();

    /** Saves the open project now. force: overwrite the cloud copy even if it's newer. */
    void saveNow (bool force = false);

    void list (std::function<void (const juce::Result&, const juce::Array<Entry>&)> done);

    /** Downloads a project into folder (replacing its files), ready to open. */
    void download (const juce::String& uuid, const juce::File& folder, std::function<void (const juce::Result&)> done);

    State getState() const noexcept    { return state; }
    juce::String getStatusText() const;

    std::function<void()> onStatusChanged;

    /** The cloud copy changed elsewhere since this computer's last save. */
    std::function<void (const juce::String& cloudUpdated)> onConflict;

    static constexpr int maxBytes = 2 * 1024 * 1024;
    static constexpr const char* stateFileName = ".stella-cloud.json";

private:
    void timerCallback() override;
    static bool collect (const juce::File& folder, juce::var& files, juce::String& hash, juce::String& error);
    void setState (State newState, const juce::String& detail = {});
    juce::var readState (const juce::File& folder) const;
    void writeState (const juce::File& folder, int version, const juce::String& hash, const juce::String& saved) const;
    static juce::String hashOf (const juce::var& files);

    FananServer& server;
    Project& project;

    State state = State::off;
    juce::String detail, pendingHash, trackedUuid;
    bool busy = false;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudProjects)
};
