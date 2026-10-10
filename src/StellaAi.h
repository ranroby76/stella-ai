// C:\workspace\Stella AI Studio\src\StellaAi.h

#pragma once

#include "FananServer.h"
#include "ProjectTools.h"

#include <functional>
#include <memory>
#include <vector>

//==============================================================================
/**
    Stella AI at work: the conversation, and the loop that lets it build.

    Each request goes to Fanan's server with the studio's tools. When the answer asks for
    tools, the studio runs them (writing files, wiring, building) and sends the results
    back, round after round, until Stella AI is done, the user presses Stop, or a step
    limit is reached. A conversation turns into building with start_building: from then
    on, for that request, the server uses its builder (the strongest model).

    A request that fails costs nothing and leaves the conversation as it was, with a note
    saying what happened. The persona and rules about the AI service live on the server.
*/
class StellaAi final
{
public:
    struct Entry
    {
        enum class Kind { user, ai, notice, activity };

        Kind kind = Kind::notice;
        juce::String text;
    };

    StellaAi (FananServer& server, ProjectTools& tools);
    ~StellaAi();

    /** Sends a request, with files for Stella AI to look at: images (shown to it), PDFs
        and text files such as code (read by it). Up to 5 files. The conversation shows
        shownText when it's given (the request without the context added for Stella AI). */
    void send (const juce::String& text, const juce::Array<juce::File>& files = {}, const juce::String& shownText = {});

    static bool canAttach (const juce::File& file);
    static juce::String attachableFiles();   // for a file chooser: "*.png;*.jpg;..."
    static constexpr int maxAttachments = 5;

    /** Stops after the step that's running (a build in progress finishes first). */
    void stop();
    void clear();

    bool isBusy() const noexcept                            { return busy; }
    const std::vector<Entry>& getEntries() const noexcept   { return entries; }

    /** Called on the message thread whenever the conversation changes. */
    std::function<void()> onChanged;

    /** A request starts, and ends (done, stopped or failed): undo snapshots go around it. */
    std::function<void (const juce::String& request)> onTurnStarted, onTurnFinished;

    static constexpr int maxRounds = 40;    // steps for one request
    static constexpr int maxHistory = 80;   // messages kept in the conversation

private:
    void sendRound();
    void onAnswer (const juce::Result& result, const juce::var& answer);
    void runNextTool();
    void finish();
    juce::Array<juce::var> compacted() const;
    static juce::Result attach (const juce::File& file, juce::Array<juce::var>& blocks);
    void trimHistory();
    void addEntry (Entry::Kind kind, const juce::String& text);
    void notify();

    FananServer& server;
    ProjectTools& tools;

    std::vector<Entry> entries;          // what the panel shows
    juce::Array<juce::var> messages;     // the conversation as the AI sees it
    juce::String mode { "chat" };        // "chat" or "build"
    int roundsLeft = 0, turnStart = 0;
    bool busy = false, stopRequested = false;
    bool layoutChangedThisStep = false;   // set_layout or edit_layout ran among this answer's calls

    juce::Array<juce::var> pendingCalls, toolResults;   // the current answer's tool calls
    juce::String currentRequest;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StellaAi)
};
