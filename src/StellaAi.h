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

    Nothing in the plugin changes without a plan the user has approved. Stella AI first
    sizes the request (propose_plan): the steps, what each one builds and how big it is,
    and, for a very big request, the phases that come after this one. The studio prices
    each step in credits from what steps of that size really cost so far, and shows the
    plan with Start, Change and Cancel. Once started, the plan is a checklist that ticks
    along with the credits each step used; a step that runs well over its estimate pauses
    and asks before going on.

    A request that fails costs nothing and leaves the conversation as it was, with a note
    saying what happened. The persona and rules about the AI service live on the server.
*/
class StellaAi final
{
public:
    struct Entry
    {
        enum class Kind { user, ai, notice, activity, plan };   // plan: the plan's card (see planText)

        Kind kind = Kind::notice;
        juce::String text;
    };

    /** What the plan waits for from the user, if anything. */
    enum class Waiting { nothing, approval, overrun };

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

    //==============================================================================
    // The plan's approval, and a step that ran over its estimate.
    Waiting getWaiting() const noexcept;
    juce::String getWaitingText() const;   // the line beside the buttons
    bool isWaitingWarning() const;         // the credits may not last

    void approvePlan();    // Start
    void cancelPlan();     // Cancel: nothing is built
    void continueStep();   // the step over its estimate goes on
    void pausePlan();      // ...or waits until the user says "continue"

    /** What a step of a size ("small", "medium", "large") really cost on average so far
        (0: no record yet), and a finished step's cost, so the estimates learn. */
    std::function<double (const juce::String& size)> averageStepCost;
    std::function<void (const juce::String& size, int credits)> onStepCost;

    /** Called on the message thread whenever the conversation changes. */
    std::function<void()> onChanged;

    /** A request starts, and ends (done, stopped or failed): undo snapshots go around it. */
    std::function<void (const juce::String& request)> onTurnStarted, onTurnFinished;

    static constexpr int maxRounds = 40;      // steps for one request
    static constexpr int maxPlanSteps = 12;   // steps in one plan: a bigger request goes in phases
    static constexpr int maxLaterPhases = 8;
    static constexpr int maxHistory = 80;     // messages kept in the conversation
    static constexpr int maxCutRetries = 2;   // answers in a row that may run out of room before it stops

    /** A step's estimate, and how far over it the step may run before it pauses. */
    static constexpr double estimateLow = 0.7, estimateHigh = 1.4, overrunFactor = 1.5;

private:
    struct Step
    {
        juce::String title, builds, size;   // size: "small", "medium" or "large"
        int low = 0, high = 0;              // the estimate, in credits
        int used = 0;                       // credits spent on it so far
        int allowance = 0;                  // past this, it pauses and asks
        bool recorded = false;              // its cost went into the averages
    };

    struct Phase
    {
        juce::String title, builds, size;
        int steps = 0, low = 0, high = 0;
    };

    bool canReachServer();   // false, with a note saying why, when nothing can be sent
    void beginTurn (const juce::var& message, const juce::String& shown, const juce::String& startMode);
    void sendRound();
    void onAnswer (const juce::Result& result, const juce::var& answer);
    void runNextTool();
    void answerDone();   // every tool call of the answer has its result
    static juce::String cutNote (bool afterCalls);   // to carry on in smaller pieces after an answer ran out of room
    void finish();
    juce::Array<juce::var> compacted() const;
    static juce::Result attach (const juce::File& file, juce::Array<juce::var>& blocks);
    void trimHistory();
    void addEntry (Entry::Kind kind, const juce::String& text);
    void notify();

    // The plan.
    juce::String proposePlan (const juce::var& input);
    juce::String runPlan (const juce::var& input);
    void chargeAnswer();                         // the answer's credits go to the step it worked on
    void recordFinishedSteps();
    void estimate (const juce::String& size, int& low, int& high) const;
    int planLow() const;
    int planHigh() const;
    int laterLow() const;
    int laterHigh() const;
    int planUsed() const;
    juce::String jobSize() const;
    bool isRunning() const noexcept;             // approved, started, not finished
    juce::String planText() const;               // the card's lines (see ChatPanel)
    juce::String planContext() const;            // for the project's description
    juce::String modeContext() const;            // what this mode may do, so a conversation hands code over to the builder
    void showPlan();
    void clearPlan();

    FananServer& server;
    ProjectTools& tools;

    std::vector<Entry> entries;          // what the panel shows
    juce::Array<juce::var> messages;     // the conversation as the AI sees it
    juce::String mode { "chat" };        // "chat" or "build"
    int roundsLeft = 0, turnStart = 0;
    bool busy = false, stopRequested = false;
    bool layoutChangedThisStep = false;   // set_layout or edit_layout ran among this answer's calls
    juce::String lastStop;                // how the last answer ended: "tools", "done", "cut"...
    int answerUsed = 0;                   // the credits the answer being handled cost
    bool cutThisAnswer = false;           // the answer being handled ran out of room
    int cutsInARow = 0;                   // answers in a row that did
    int turnCredits = 0;                  // the credits this request has cost so far

    juce::Array<juce::var> pendingCalls, toolResults;   // the current answer's tool calls
    juce::String currentRequest;

    std::vector<Step> steps;
    std::vector<Phase> later;            // phases after this plan (a very big request)
    juce::String summary;                // what the plan builds, in a sentence or two
    juce::String planMode { "build" };   // where it was proposed, and where it runs
    juce::String planRequest;            // the request it was made for
    int planCurrent = 0;                 // 1-based: the step under way (0: not started)
    bool planDone = false, planCancelled = false;
    bool planFinishing = false;          // done: true came: the plan is done when this request finishes
    bool proposalPending = false;        // shown, waiting for Start, Change or Cancel
    bool planApproved = false;
    bool proposedThisAnswer = false;
    bool userSpokeSinceProposal = false;  // a typed "yes, go ahead" may start it
    bool overrunPaused = false;
    int sizingCredits = 0;               // spent before the plan was shown (reading, sizing)
    static constexpr size_t noEntry = ~(size_t) 0;
    size_t planEntry = noEntry;          // its card in entries

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StellaAi)
};
