// C:\workspace\Stella AI Studio\src\StellaAi.cpp

#include "StellaAi.h"

#include <juce_graphics/juce_graphics.h>

namespace
{
    juce::var makeMessage (const juce::String& role, const juce::var& content)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("role", role);
        object->setProperty ("content", content);
        return juce::var (object);
    }

    juce::var makeToolResult (const juce::String& id, const juce::String& text, bool isError)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("type", "tool_result");
        object->setProperty ("tool_use_id", id);
        object->setProperty ("content", text.isNotEmpty() ? text : juce::String ("(done)"));

        if (isError)
            object->setProperty ("is_error", true);

        return juce::var (object);
    }

    /** One of the user's own messages (with or without attachments), not tool results. */
    bool isPlainUserMessage (const juce::var& message)
    {
        if (message.getProperty ("role", {}).toString() != "user")
            return false;

        const auto content = message.getProperty ("content", {});

        if (content.isString())
            return true;

        if (const auto* blocks = content.getArray())
            for (const auto& block : *blocks)
                if (block.getProperty ("type", {}).toString() == "tool_result")
                    return false;

        return true;
    }

    const juce::StringArray imageTypes { "png", "jpg", "jpeg", "gif", "webp" };
    const juce::StringArray textTypes { "txt", "md", "cpp", "h", "hpp", "c", "cc", "json", "csv", "xml", "ini", "cfg",
                                        "log", "py", "js", "ts", "html", "css", "yaml", "yml", "toml" };

    juce::var blockOf (std::initializer_list<std::pair<const char*, juce::var>> fields)
    {
        auto* object = new juce::DynamicObject();

        for (const auto& [key, value] : fields)
            object->setProperty (key, value);

        return juce::var (object);
    }
}

//==============================================================================
StellaAi::StellaAi (FananServer& s, ProjectTools& t)
    : server (s), tools (t)
{
}

StellaAi::~StellaAi()
{
    *alive = false;   // answers and tool results arriving later find no one
}

void StellaAi::addEntry (Entry::Kind kind, const juce::String& text)
{
    entries.push_back ({ kind, text });
}

void StellaAi::notify()
{
    if (onChanged != nullptr)
        onChanged();
}

void StellaAi::clear()
{
    if (busy)
        return;

    entries.clear();
    messages.clear();
    clearPlan();
    later.clear();
    notify();
}

void StellaAi::stop()
{
    if (! busy)
        return;

    stopRequested = true;
    addEntry (Entry::Kind::activity, juce::String::fromUTF8 ("Stopping after this step\xe2\x80\xa6"));
    notify();
}

void StellaAi::finish()
{
    // The last step finished and Stella AI answered: the plan is done.
    if (! steps.empty() && planApproved && ! planDone && (planFinishing || (lastStop == "done" && planCurrent >= (int) steps.size())))
    {
        planDone = true;
        planFinishing = false;
        planCurrent = (int) steps.size();
        recordFinishedSteps();
        showPlan();
    }

    busy = false;
    pendingCalls.clear();
    toolResults.clear();
    notify();

    if (onTurnFinished != nullptr)
        onTurnFinished (currentRequest);
}

//==============================================================================
bool StellaAi::canAttach (const juce::File& file)
{
    const auto extension = file.getFileExtension().trimCharactersAtStart (".").toLowerCase();
    return file.existsAsFile() && (imageTypes.contains (extension) || textTypes.contains (extension) || extension == "pdf");
}

juce::String StellaAi::attachableFiles()
{
    juce::StringArray patterns;

    for (const auto& type : imageTypes)  patterns.add ("*." + type);
    patterns.add ("*.pdf");
    for (const auto& type : textTypes)   patterns.add ("*." + type);

    return patterns.joinIntoString (";");
}

juce::Result StellaAi::attach (const juce::File& file, juce::Array<juce::var>& blocks)
{
    const auto extension = file.getFileExtension().trimCharactersAtStart (".").toLowerCase();
    const auto name = file.getFileName();

    if (textTypes.contains (extension))
    {
        if (file.getSize() > 150 * 1024)
            return juce::Result::fail ("it's over 150 KB of text");

        blocks.add (blockOf ({ { "type", "text" }, { "text", "Attached file " + name + ":\n```\n" + file.loadFileAsString() + "\n```" } }));
        return juce::Result::ok();
    }

    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
        return juce::Result::fail ("it couldn't be read");

    if (extension == "pdf")
    {
        if (data.getSize() > 3 * 1024 * 1024)
            return juce::Result::fail ("PDFs can be up to 3 MB");

        blocks.add (blockOf ({ { "type", "document" },
                               { "source", blockOf ({ { "type", "base64" }, { "media_type", "application/pdf" },
                                                      { "data", juce::Base64::toBase64 (data.getData(), data.getSize()) } }) },
                               { "title", name } }));
        return juce::Result::ok();
    }

    // Images: shown to Stella AI. Large ones are scaled down (cheaper, and it sees no more anyway).
    juce::String mediaType = "image/webp";

    if (extension != "webp")
    {
        auto image = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());

        if (! image.isValid())
            return juce::Result::fail ("it isn't an image the studio can read");

        constexpr int longest = 1568;
        const auto scale = (float) longest / (float) juce::jmax (image.getWidth(), image.getHeight());

        if (scale < 1.0f)
            image = image.rescaled (juce::jmax (1, juce::roundToInt ((float) image.getWidth() * scale)),
                                    juce::jmax (1, juce::roundToInt ((float) image.getHeight() * scale)),
                                    juce::Graphics::highResamplingQuality);

        juce::MemoryOutputStream encoded;

        if (image.hasAlphaChannel())
        {
            juce::PNGImageFormat().writeImageToStream (image, encoded);
            mediaType = "image/png";
        }
        else
        {
            juce::JPEGImageFormat jpeg;
            jpeg.setQuality (0.85f);
            jpeg.writeImageToStream (image, encoded);
            mediaType = "image/jpeg";
        }

        data = encoded.getMemoryBlock();
    }

    if (data.getSize() > (size_t) 3500 * 1024)
        return juce::Result::fail ("the image is too big (over 3.5 MB)");

    blocks.add (blockOf ({ { "type", "image" },
                           { "source", blockOf ({ { "type", "base64" }, { "media_type", mediaType },
                                                  { "data", juce::Base64::toBase64 (data.getData(), data.getSize()) } }) } }));
    return juce::Result::ok();
}

void StellaAi::send (const juce::String& text, const juce::Array<juce::File>& files, const juce::String& shownText)
{
    auto request = text.trim();

    if (busy || (request.isEmpty() && files.isEmpty()))
        return;

    // The files, read and made ready; any that can't be used are named in a note.
    juce::Array<juce::var> blocks;
    juce::StringArray attached, problems;

    for (const auto& file : files)
    {
        if (attached.size() >= maxAttachments)
        {
            problems.add (file.getFileName() + " (at most " + juce::String (maxAttachments) + " files at a time)");
            continue;
        }

        if (const auto result = attach (file, blocks); result.wasOk())
            attached.add (file.getFileName());
        else
            problems.add (file.getFileName() + " (" + result.getErrorMessage() + ")");
    }

    if (request.isEmpty() && attached.isEmpty())
    {
        addEntry (Entry::Kind::notice, "Not sent: " + problems.joinIntoString (", ") + ".");
        notify();
        return;
    }

    if (request.isEmpty())
        request = "Have a look at the attached file" + juce::String (attached.size() > 1 ? "s." : ".");

    addEntry (Entry::Kind::user, (shownText.trim().isNotEmpty() ? shownText.trim() : request)
                                     + (attached.isEmpty() ? juce::String()
                                                           : juce::String::fromUTF8 ("\n\xf0\x9f\x93\x8e ") + attached.joinIntoString (", ")));

    if (! problems.isEmpty())
        addEntry (Entry::Kind::notice, "Not attached: " + problems.joinIntoString (", ") + ".");

    if (! canReachServer())
        return;

    currentRequest = shownText.trim().isNotEmpty() ? shownText.trim() : request;

    if (planDone || planCancelled)
        clearPlan();   // a finished plan isn't told again; an unfinished one is, so "continue" carries on

    auto startMode = juce::String ("chat");

    if (proposalPending)
    {
        // About the plan on show: "yes, go ahead" starts it, anything else changes it (or
        // is answered while it waits). Its own mode knows it best.
        userSpokeSinceProposal = true;
        startMode = planMode;
    }
    else if (planApproved && ! planDone && planCurrent > 0)
    {
        // Typing on after a step ran over its estimate means going on with it.
        overrunPaused = false;
        auto& step = steps[(size_t) planCurrent - 1];

        if (step.used >= step.allowance)
            step.allowance = step.used + juce::roundToInt (step.high * overrunFactor);
    }

    if (blocks.isEmpty())
    {
        beginTurn (makeMessage ("user", request), currentRequest, startMode);
    }
    else
    {
        blocks.add (blockOf ({ { "type", "text" }, { "text", request } }));
        beginTurn (makeMessage ("user", blocks), currentRequest, startMode);
    }
}

bool StellaAi::canReachServer()
{
    // Nothing is sent (or charged) unless the server can take it.
    if (! server.isReady())
    {
        addEntry (Entry::Kind::notice, server.getStatus() == FananServer::Status::noInternet
                                           ? "Not sent: there's no internet connection. Check it and send again."
                                           : "Not sent: Fanan's server can't be reached right now. Please try again in a moment.");
        server.checkNow();
        notify();
        return false;
    }

    if (server.getCredits() == 0)
    {
        addEntry (Entry::Kind::notice, "Not sent: you're out of Stella AI credits. Use Buy credits to get more, then send again.");
        notify();
        return false;
    }

    return true;
}

void StellaAi::beginTurn (const juce::var& message, const juce::String& shown, const juce::String& startMode)
{
    if (onTurnStarted != nullptr)
        onTurnStarted (shown);

    trimHistory();
    turnStart = messages.size();
    messages.add (message);

    mode = startMode;
    lastStop.clear();
    roundsLeft = maxRounds;
    stopRequested = false;
    turnCredits = 0;
    busy = true;

    notify();
    sendRound();
}

void StellaAi::sendRound()
{
    auto* body = new juce::DynamicObject();
    const juce::var payload (body);

    body->setProperty ("mode", mode);
    body->setProperty ("guide", ProjectTools::getGuide());
    body->setProperty ("context", tools.describeProject() + modeContext() + planContext());
    body->setProperty ("messages", compacted());
    body->setProperty ("tools", tools.getDefinitions (mode == "build"));
    body->setProperty ("request_id", juce::Uuid().toString());

    server.chat (payload, [this, stillAlive = alive] (const juce::Result& result, const juce::var& answer)
    {
        if (*stillAlive)
            onAnswer (result, answer);
    });
}

void StellaAi::onAnswer (const juce::Result& result, const juce::var& answer)
{
    if (result.failed())
    {
        // The request didn't happen: the conversation goes back to before it (files already
        // written stay in the project, and the AI sees them next time).
        messages.removeRange (turnStart, messages.size() - turnStart);
        addEntry (Entry::Kind::notice, result.getErrorMessage());
        finish();
        return;
    }

    const auto content = answer.getProperty ("content", {});
    const auto reply = answer.getProperty ("reply", {}).toString().trim();
    const auto stopReason = answer.getProperty ("stop", {}).toString();
    lastStop = stopReason;
    answerUsed = juce::jmax (0, (int) answer.getProperty ("used", 0));
    proposedThisAnswer = false;

    messages.add (makeMessage ("assistant", content.isArray() && content.getArray()->size() > 0 ? content
                                                                                                : juce::var (reply.isNotEmpty() ? reply : juce::String ("(no reply)"))));

    if (reply.isNotEmpty())
        addEntry (Entry::Kind::ai, reply);

    if (stopReason == "tools" && content.isArray())
    {
        pendingCalls.clear();
        toolResults.clear();

        for (const auto& block : *content.getArray())
            if (block.getProperty ("type", {}).toString() == "tool_use")
                pendingCalls.add (block);

        layoutChangedThisStep = false;
        notify();
        runNextTool();
        return;
    }

    chargeAnswer();

    if (stopReason == "cut")
        addEntry (Entry::Kind::notice, "That answer was too long and got cut off. Ask for less at once, or say \"continue\".");
    else if (stopReason == "refused")
        addEntry (Entry::Kind::notice, "Stella AI declined that request.");
    else if (reply.isEmpty())
        addEntry (Entry::Kind::notice, "Stella AI finished without a reply.");

    finish();
}

//==============================================================================
void StellaAi::runNextTool()
{
    if (pendingCalls.isEmpty())
    {
        answerDone();
        return;
    }

    const auto call = pendingCalls.removeAndReturn (0);
    const auto name = call.getProperty ("name", {}).toString();
    const auto id = call.getProperty ("id", {}).toString();
    const auto input = call.getProperty ("input", {});

    if (name == "propose_plan" || name == "plan")
    {
        const auto result = name == "plan" ? runPlan (input) : proposePlan (input);
        toolResults.add (makeToolResult (id, result, result.startsWith ("Not ")));
        notify();
        runNextTool();
        return;
    }

    if (name == "start_building")
    {
        // From here on, this request goes to the builder.
        mode = "build";
        addEntry (Entry::Kind::activity, juce::String::fromUTF8 ("Handing over to the builder\xe2\x80\xa6"));
        toolResults.add (makeToolResult (id, "The builder has taken over. Look at the project as you need, then size the request with "
                                             "propose_plan: nothing changes before the user approves it.", false));
        runNextTool();
        return;
    }

    // Nothing in the plugin changes without a plan the user approved.
    static const juce::StringArray changeTools { "set_layout", "edit_layout", "save_preset", "write_file", "delete_file", "set_graph", "build" };

    if (changeTools.contains (name) && ! (planApproved && ! planDone))
    {
        toolResults.add (makeToolResult (id, proposalPending
                                                 ? "Not done: the plan is waiting for the user's approval. Nothing changes until they start it."
                                                 : "Not done: nothing in the plugin changes before the user approves a plan. Size this request "
                                                   "with propose_plan first (read_project and read_file are fine before it).",
                                         true));
        runNextTool();
        return;
    }

    // edit_layout names elements by the indexes Stella AI was shown: after another change to
    // the GUI in the same step they'd point at the wrong elements.
    if (name == "edit_layout" && layoutChangedThisStep)
    {
        toolResults.add (makeToolResult (id, "Not done: the GUI changed just before in this step, so these indexes are out of date. "
                                             "Make this edit in your next step, with the updated \"GUI elements\" list.", true));
        runNextTool();
        return;
    }

    if (const auto line = ProjectTools::describeCall (name, input); line.isNotEmpty())
    {
        addEntry (Entry::Kind::activity, line);
        notify();
    }

    tools.run (name, input, [this, stillAlive = alive, id, name] (const juce::String& result, bool isError)
    {
        if (! *stillAlive)
            return;

        toolResults.add (makeToolResult (id, result, isError));

        if (! isError && (name == "set_layout" || name == "edit_layout"))
            layoutChangedThisStep = true;

        if (name == "build")
            addEntry (Entry::Kind::activity, isError ? juce::String::fromUTF8 ("Fixing a few things\xe2\x80\xa6")
                                                     : juce::String ("Ready: playing live"));

        runNextTool();
    });
}

void StellaAi::answerDone()
{
    // Every call answered: the results go back as the next turn.
    messages.add (makeMessage ("user", toolResults));
    toolResults.clear();
    chargeAnswer();

    // The plan is on show: the user starts it, changes it or cancels it.
    if (proposedThisAnswer)
    {
        messages.add (makeMessage ("assistant", juce::var ("(I've shown the user my plan and its price, and I'm waiting for them "
                                                           "to start it, change it or cancel it.)")));
        finish();
        return;
    }

    if (stopRequested)
    {
        messages.add (makeMessage ("assistant", juce::var ("(The user stopped me here.)")));
        addEntry (Entry::Kind::notice, "Stopped.");
        finish();
        return;
    }

    // A step well over its estimate pauses: the user decides whether it goes on.
    if (planApproved && ! planDone && ! planFinishing && planCurrent > 0)
    {
        const auto& step = steps[(size_t) planCurrent - 1];

        if (step.used > step.allowance)
        {
            overrunPaused = true;
            messages.add (makeMessage ("assistant", juce::var ("(I paused: this step is taking longer than planned, and the user decides "
                                                               "whether I go on.)")));
            addEntry (Entry::Kind::notice, "Step " + juce::String (planCurrent) + " is taking longer than planned: " + juce::String (step.used)
                                               + " credits so far (estimated " + juce::String (step.low) + juce::String::fromUTF8 ("\xe2\x80\x93")
                                               + juce::String (step.high) + "). Go on, or pause it below.");
            finish();
            return;
        }
    }

    if (--roundsLeft <= 0)
    {
        messages.add (makeMessage ("assistant", juce::var ("(I paused here after many steps.)")));
        addEntry (Entry::Kind::notice, "Stella AI paused after many steps. Say \"continue\" to carry on.");
        finish();
        return;
    }

    notify();
    sendRound();
}

//==============================================================================
StellaAi::Waiting StellaAi::getWaiting() const noexcept
{
    if (busy)
        return Waiting::nothing;

    if (proposalPending)
        return Waiting::approval;

    if (overrunPaused && planApproved && ! planDone && planCurrent > 0)
        return Waiting::overrun;

    return Waiting::nothing;
}

juce::String StellaAi::getWaitingText() const
{
    const auto dash = juce::String::fromUTF8 ("\xe2\x80\x93");
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    switch (getWaiting())
    {
        case Waiting::approval:
        {
            const auto credits = server.getCredits();
            return juce::String ((int) steps.size()) + ((int) steps.size() == 1 ? " step" : " steps") + (later.empty() ? juce::String() : juce::String (" (phase 1)"))
                 + dot + "about " + juce::String (planLow()) + dash + juce::String (planHigh()) + " credits"
                 + (credits >= 0 ? dot + "you have " + juce::String (credits) : juce::String());
        }

        case Waiting::overrun:
        {
            const auto& step = steps[(size_t) planCurrent - 1];
            return "Step " + juce::String (planCurrent) + ": " + juce::String (step.used) + " credits so far, estimated "
                 + juce::String (step.low) + dash + juce::String (step.high);
        }

        case Waiting::nothing:
        default:
            return {};
    }
}

bool StellaAi::isWaitingWarning() const
{
    const auto credits = server.getCredits();
    return getWaiting() == Waiting::approval && credits >= 0 && credits < planHigh();
}

void StellaAi::approvePlan()
{
    if (busy || ! proposalPending || ! canReachServer())
        return;

    proposalPending = false;
    planApproved = true;
    planCurrent = 0;
    showPlan();

    currentRequest = planRequest;
    addEntry (Entry::Kind::user, "Start the plan.");
    beginTurn (makeMessage ("user", "I approve the plan: start it now. Do the steps one at a time, calling plan with current as each one "
                                    "starts and done: true after the last."),
               planRequest, planMode);
}

void StellaAi::cancelPlan()
{
    if (busy || ! proposalPending)
        return;

    proposalPending = false;
    planApproved = false;
    planCancelled = true;
    later.clear();
    showPlan();

    messages.add (makeMessage ("user", "(I cancelled the plan you proposed: don't build it.)"));
    messages.add (makeMessage ("assistant", juce::var ("(Cancelled: nothing was built.)")));
    notify();
}

void StellaAi::continueStep()
{
    if (busy || getWaiting() != Waiting::overrun || ! canReachServer())
        return;

    overrunPaused = false;
    auto& step = steps[(size_t) planCurrent - 1];
    step.allowance = step.used + juce::roundToInt (step.high * overrunFactor);

    currentRequest = planRequest;
    addEntry (Entry::Kind::user, "Go on.");
    beginTurn (makeMessage ("user", "Go on with this step."), planRequest, planMode);
}

void StellaAi::pausePlan()
{
    if (busy || getWaiting() != Waiting::overrun)
        return;

    overrunPaused = false;
    addEntry (Entry::Kind::activity, "Paused at step " + juce::String (planCurrent) + ". Say \"continue\" when you want to go on.");
    notify();
}

//==============================================================================
juce::String StellaAi::proposePlan (const juce::var& input)
{
    auto sizeOf = [] (const juce::var& value)
    {
        const auto size = value.toString().trim().toLowerCase();
        return size == "small" || size == "large" ? size : juce::String ("medium");
    };

    auto text = [] (const juce::var& value, int length)
    {
        return value.toString().replaceCharacters ("\r\n", "  ").trim().substring (0, length);
    };

    std::vector<Step> newSteps;

    if (const auto* list = input.getProperty ("steps", {}).getArray())
    {
        for (const auto& item : *list)
        {
            Step step;

            if (item.isObject())
            {
                step.title = text (item.getProperty ("title", {}), 80);
                step.builds = text (item.getProperty ("builds", {}), 200);
                step.size = sizeOf (item.getProperty ("size", {}));
            }
            else
            {
                step.title = text (item, 80);
                step.size = "medium";
            }

            if (step.title.isNotEmpty())
                newSteps.push_back (step);
        }
    }

    if (newSteps.empty())
        return "Not done: the plan has no steps. Give its steps, each with title, builds and size.";

    if ((int) newSteps.size() > maxPlanSteps)
        return "Not done: at most " + juce::String (maxPlanSteps) + " steps in one plan. Make this plan the first phase and put the rest in "
               "later, as phases with their number of steps.";

    std::vector<Phase> newLater;

    if (const auto* list = input.getProperty ("later", {}).getArray())
    {
        for (const auto& item : *list)
        {
            Phase phase;
            phase.title = text (item.getProperty ("title", {}), 80);
            phase.builds = text (item.getProperty ("builds", {}), 200);
            phase.size = sizeOf (item.getProperty ("size", {}));
            phase.steps = juce::jlimit (1, maxPlanSteps, (int) item.getProperty ("steps", 4));

            if (phase.title.isNotEmpty())
                newLater.push_back (phase);
        }
    }

    if ((int) newLater.size() > maxLaterPhases)
        return "Not done: at most " + juce::String (maxLaterPhases) + " later phases. Make each phase bigger (up to 12 steps).";

    // Priced from what steps of each size really cost so far.
    for (auto& step : newSteps)
    {
        estimate (step.size, step.low, step.high);
        step.allowance = juce::roundToInt (step.high * overrunFactor);
    }

    for (auto& phase : newLater)
    {
        estimate (phase.size, phase.low, phase.high);
        phase.low *= phase.steps;
        phase.high *= phase.steps;
    }

    steps = std::move (newSteps);
    later = std::move (newLater);
    summary = text (input.getProperty ("summary", {}), 400);
    planMode = mode;
    planRequest = currentRequest;
    planCurrent = 0;
    planDone = planCancelled = planFinishing = false;
    proposalPending = true;
    planApproved = false;
    proposedThisAnswer = true;
    userSpokeSinceProposal = false;
    overrunPaused = false;

    entries.push_back ({ Entry::Kind::plan, {} });
    planEntry = entries.size() - 1;
    showPlan();

    return "The user sees your plan and its price. Stop here and write nothing more: they start it, change it or cancel it.";
}

juce::String StellaAi::runPlan (const juce::var& input)
{
    if (steps.empty() || planCancelled)
        return "Not done: there's no plan. Propose one with propose_plan.";

    if (proposalPending)
    {
        // The user answered the plan in words ("yes, go ahead"): starting it is the approval.
        if ((int) input.getProperty ("current", 0) >= 1 && userSpokeSinceProposal && ! proposedThisAnswer)
        {
            proposalPending = false;
            planApproved = true;
            planCurrent = 0;
        }
        else
        {
            return "Not done: the plan is waiting for the user's approval. Stop here: they start it, change it or cancel it.";
        }
    }

    if (! planApproved || planDone)
        return "Not done: there's no approved plan under way. Propose one with propose_plan.";

    const auto numSteps = (int) steps.size();

    if (input.hasProperty ("current"))
    {
        const auto current = juce::jlimit (0, numSteps, (int) input.getProperty ("current", 0));

        if (current != planCurrent && current > 0)
            addEntry (Entry::Kind::activity, "Step " + juce::String (current) + " of " + juce::String (numSteps) + ": " + steps[(size_t) current - 1].title);

        if (current > 0)
            planCurrent = current;
    }

    if ((bool) input.getProperty ("done", false))
        planFinishing = true;   // done when the request finishes, with every credit it cost counted

    showPlan();

    if (planFinishing)
        return "Plan done: all " + juce::String (numSteps) + " steps.";

    return "The user sees the plan" + (planCurrent > 0 ? ", now at step " + juce::String (planCurrent) + " of " + juce::String (numSteps)
                                                        : juce::String (" (" + juce::String (numSteps) + " steps)"))
         + ". Do the steps one at a time, calling plan with current as each starts and done: true after the last.";
}

void StellaAi::chargeAnswer()
{
    const auto used = answerUsed;
    answerUsed = 0;
    turnCredits += used;

    if (planApproved && ! planDone && ! steps.empty())
    {
        if (planCurrent == 0)
            planCurrent = 1;   // working without saying so: it's the first step

        steps[(size_t) planCurrent - 1].used += used;
    }

    if (proposedThisAnswer)
        sizingCredits = turnCredits;   // reading the project and sizing it, before the plan showed

    if (planFinishing)
        planCurrent = (int) steps.size();   // done once the answer closing the request is counted too (finish)

    recordFinishedSteps();
    showPlan();
}

void StellaAi::recordFinishedSteps()
{
    // Finished steps teach the estimates what steps of their size really cost.
    for (size_t i = 0; i < steps.size(); ++i)
    {
        auto& step = steps[i];
        const bool finished = planDone || (int) i + 1 < planCurrent;

        if (finished && ! step.recorded && step.used > 0)
        {
            step.recorded = true;

            if (onStepCost != nullptr)
                onStepCost (step.size, step.used);
        }
    }
}

void StellaAi::estimate (const juce::String& size, int& low, int& high) const
{
    // A first guess per size until real steps have been measured (credits per step of the
    // builder: one round of thinking and writing a file is about 8, a full module with a fix
    // about 18, a big part with helpers and fixes about 35).
    const double guess = size == "small" ? 8.0 : size == "large" ? 35.0 : 18.0;
    const auto measured = averageStepCost != nullptr ? averageStepCost (size) : 0.0;
    const auto average = measured > 0.0 ? measured : guess;

    low = juce::jmax (1, juce::roundToInt (average * estimateLow));
    high = juce::jmax (low + 1, juce::roundToInt (average * estimateHigh));
}

int StellaAi::planLow() const    { int total = 0; for (const auto& s : steps) total += s.low;  return total; }
int StellaAi::planHigh() const   { int total = 0; for (const auto& s : steps) total += s.high; return total; }
int StellaAi::laterLow() const   { int total = 0; for (const auto& p : later) total += p.low;  return total; }
int StellaAi::laterHigh() const  { int total = 0; for (const auto& p : later) total += p.high; return total; }
int StellaAi::planUsed() const   { int total = 0; for (const auto& s : steps) total += s.used; return total; }

juce::String StellaAi::jobSize() const
{
    const auto high = planHigh() + laterHigh();
    return high <= 30 ? "Small" : high <= 100 ? "Medium" : high <= 300 ? "Large" : "Huge";
}

juce::String StellaAi::planText() const
{
    // One line each; the first character tells the panel how to show it: "#" the title,
    // "~" what it builds, a mark for each step, "+" a later phase, "=" a note, "!" a warning.
    const auto dash = juce::String::fromUTF8 ("\xe2\x80\x93");
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto range = [&dash] (int low, int high) { return juce::String (low) + dash + juce::String (high); };
    const auto numSteps = (int) steps.size();
    const auto stepWord = juce::String (numSteps == 1 ? " step" : " steps");

    juce::StringArray lines;

    if (planCancelled)
        lines.add ("# Plan cancelled" + dot + "nothing was built");
    else if (planDone)
        lines.add ("# Plan done" + dot + juce::String (numSteps) + stepWord + dot + juce::String (planUsed()) + " credits (estimated "
                   + range (planLow(), planHigh()) + ")");
    else if (planApproved && planCurrent > 0)
        lines.add ("# Plan" + dot + "step " + juce::String (planCurrent) + " of " + juce::String (numSteps) + dot + juce::String (planUsed())
                   + " credits so far (estimated " + range (planLow(), planHigh()) + ")");
    else if (planApproved)
        lines.add ("# Plan" + dot + "starting" + dot + "estimated " + range (planLow(), planHigh()) + " credits");
    else
        lines.add ("# Plan" + dot + jobSize() + " job" + (later.empty() ? juce::String() : dot + "phase 1 of " + juce::String ((int) later.size() + 1)));

    if (summary.isNotEmpty())
        lines.add ("~ " + summary);

    for (int i = 0; i < numSteps; ++i)
    {
        const auto& step = steps[(size_t) i];
        const auto number = i + 1;
        const bool done = planDone || (planApproved && number < planCurrent);
        const bool now = ! done && planApproved && number == planCurrent;

        const auto mark = done ? juce::String::fromUTF8 ("\xe2\x9c\x93 ")   // ✓
                        : now  ? juce::String::fromUTF8 ("\xe2\x96\xb8 ")   // ▸
                               : juce::String::fromUTF8 ("\xe2\x97\x8b ");  // ○

        const auto cost = done ? juce::String (step.used) + " credits"
                        : now  ? juce::String (step.used) + " so far (" + range (step.low, step.high) + ")"
                               : range (step.low, step.high) + " credits";

        lines.add (mark + juce::String (number) + ". " + step.title
                   + (step.builds.isNotEmpty() ? juce::String::fromUTF8 (" \xe2\x80\x94 ") + step.builds : juce::String())
                   + dot + cost);
    }

    const auto later0 = later.empty() ? nullptr : &later.front();

    if (proposalPending)
    {
        lines.add ("= This plan: about " + range (planLow(), planHigh()) + " credits");

        for (const auto& phase : later)
            lines.add ("+ Later: " + phase.title + (phase.builds.isNotEmpty() ? juce::String::fromUTF8 (" \xe2\x80\x94 ") + phase.builds : juce::String())
                       + dot + juce::String (phase.steps) + (phase.steps == 1 ? " step" : " steps") + dot + range (phase.low, phase.high) + " credits");

        if (! later.empty())
            lines.add ("= The whole job, all phases: about " + range (planLow() + laterLow(), planHigh() + laterHigh()) + " credits");

        const auto credits = server.getCredits();
        lines.add ("= Planning it used " + juce::String (sizingCredits) + (sizingCredits == 1 ? " credit" : " credits")
                   + (credits >= 0 ? dot + "you have " + juce::String (credits) : juce::String()));

        if (credits >= 0 && credits < planHigh())
            lines.add ("! That may not be enough for all of this plan: buy credits first, or it may stop partway (it carries on after).");
    }
    else if (planDone && later0 != nullptr)
    {
        lines.add ("= Next: " + later0->title + dot + "say \"next phase\" to plan it");
    }

    return lines.joinIntoString ("\n");
}

juce::String StellaAi::modeContext() const
{
    // A conversation can design the panel but can't write code: a GUI element the toolbox
    // lacks (a drop-down list, say) is the builder's to program, never a toolbox stand-in.
    if (mode == "build")
        return "\nYou're the builder. Size every request with propose_plan before changing anything. A drop-down list (droplist, "
               "combo box) is the toolbox's \"dropdown\". When the request needs a GUI element the toolbox doesn't have and the "
               "project hasn't programmed (a menu, tabs, a step sequencer, a rotary switch...), program it in elements/<Name>.cpp, "
               "build, then put it on the panel. Never make do with a selector or another toolbox type instead, and never tell the "
               "user it can't be done.\n";

    return "\nYou're in a conversation: you can't write code or build. When the request needs code (a new sound, a change to one, "
           "a GUI element the toolbox doesn't have and the project hasn't programmed: a menu, tabs, a step sequencer, a rotary "
           "switch...), call start_building straight away: the builder sizes it, programs it and puts it on the panel. A change "
           "only to the panel (toolbox types, a drop-down list being the toolbox's \"dropdown\", and the programmed elements listed "
           "above) or to presets: size it with propose_plan first, then do it once the user starts it. Never make do with a "
           "selector or another toolbox type instead of a missing element, and never tell the user something can't be done.\n";
}

juce::String StellaAi::planContext() const
{
    juce::String text;

    if (! steps.empty() && ! planDone && ! planCancelled)
    {
        if (proposalPending)
            text << "\nYour plan for the request \"" << planRequest.substring (0, 200) << "\" is on show with its price, waiting for the user:\n";
        else
            text << "\nYour approved plan for the request \"" << planRequest.substring (0, 200) << "\" (the user sees it as a checklist with "
                    "the credits each step used):\n";

        for (size_t i = 0; i < steps.size(); ++i)
        {
            const auto number = (int) i + 1;
            text << number << ". " << steps[i].title << " (" << steps[i].size << "): " << steps[i].builds
                 << (! proposalPending && number < planCurrent ? " (done)" : ! proposalPending && number == planCurrent ? " (under way)" : "") << "\n";
        }

        if (proposalPending)
            text << "Nothing changes until the user starts it. If their message approves it (yes, go ahead, start), call plan with current 1 "
                    "and begin. If they want it changed, propose the changed plan with propose_plan. If they ask something else, answer "
                    "it: the plan keeps waiting.\n";
        else
            text << "Carry on from the step under way (or the first one not done), one step at a time, calling plan with current as each "
                    "starts and done: true after the last, unless the user now asks for something else: then propose a new plan.\n";
    }

    if (! later.empty() && ! planCancelled)
    {
        text << "Phases still to come after this plan, as you proposed them:\n";

        for (size_t i = 0; i < later.size(); ++i)
            text << (int) i + 1 << ". " << later[i].title << ": " << later[i].builds << " (" << later[i].steps << " steps, mostly "
                 << later[i].size << ")\n";

        text << "When the user asks for the next phase, propose it with propose_plan: its steps, and the phases left after it in later.\n";
    }

    return text;
}

void StellaAi::showPlan()
{
    if (planEntry < entries.size() && entries[planEntry].kind == Entry::Kind::plan)
        entries[planEntry].text = planText();
}

void StellaAi::clearPlan()
{
    // The phases still to come stay: "next phase" plans the next one.
    steps.clear();
    planCurrent = 0;
    planDone = planCancelled = planFinishing = false;
    proposalPending = planApproved = proposedThisAnswer = userSpokeSinceProposal = overrunPaused = false;
    planRequest.clear();
    summary.clear();
    sizingCredits = 0;
    planEntry = noEntry;   // its card stays as it last was
}

//==============================================================================
juce::Array<juce::var> StellaAi::compacted() const
{
    // Old steps are sent short: a file written earlier is in the project now (the AI can
    // read it again), and long old tool results are cut. That keeps every request small.
    int lastAssistant = -1, lastUser = -1;

    for (int i = messages.size(); --i >= 0;)
    {
        const auto role = messages.getReference (i).getProperty ("role", {}).toString();

        if (role == "assistant" && lastAssistant < 0) lastAssistant = i;
        if (role == "user" && lastUser < 0)           lastUser = i;
    }

    juce::Array<juce::var> result;

    for (int i = 0; i < messages.size(); ++i)
    {
        const auto& message = messages.getReference (i);
        const auto content = message.getProperty ("content", {});

        // The request being worked on keeps its attachments whole for every step.
        if (! content.isArray() || i == lastAssistant || i == lastUser || (i >= turnStart && isPlainUserMessage (message)))
        {
            result.add (message);
            continue;
        }

        // Earlier requests: their tool steps are told as plain text, so the request being
        // sent only carries tool blocks of its own (whatever tools this request offers).
        if (i < turnStart)
        {
            juce::StringArray lines;

            for (const auto& block : *content.getArray())
            {
                const auto type = block.getProperty ("type", {}).toString();

                if (type == "text")
                {
                    const auto text = block.getProperty ("text", {}).toString();
                    lines.add (text.length() > 1500 ? text.substring (0, 1500) + "\n[... cut]" : text);
                }
                else if (type == "tool_use")
                {
                    const auto input = block.getProperty ("input", {});
                    const auto path = input.getProperty ("path", input.getProperty ("name", {})).toString();
                    lines.add ("(Earlier step: " + block.getProperty ("name", {}).toString() + (path.isNotEmpty() ? " " + path : juce::String()) + ")");
                }
                else if (type == "tool_result")
                {
                    const auto text = block.getProperty ("content", {}).toString();
                    lines.add ("(Result: " + (text.length() > 300 ? text.substring (0, 300) + juce::String::fromUTF8 ("\xe2\x80\xa6") : text) + ")");
                }
                else if (type == "image" || type == "document")
                {
                    lines.add (type == "image" ? "[an image the user attached earlier]" : "[a PDF the user attached earlier]");
                }
            }

            result.add (makeMessage (message.getProperty ("role", {}).toString(), lines.joinIntoString ("\n")));
            continue;
        }

        juce::Array<juce::var> blocks;

        for (const auto& block : *content.getArray())
        {
            auto copy = block.clone();
            const auto type = copy.getProperty ("type", {}).toString();

            if (type == "tool_use" && copy.getProperty ("name", {}).toString() == "write_file")
            {
                auto input = copy.getProperty ("input", {}).clone();
                const auto lines = juce::StringArray::fromLines (input.getProperty ("content", {}).toString()).size();

                if (auto* object = input.getDynamicObject())
                    object->setProperty ("content", "[" + juce::String (lines) + " lines, saved in the project]");

                copy.getDynamicObject()->setProperty ("input", input);
            }
            else if (type == "tool_result")
            {
                const auto text = copy.getProperty ("content", {}).toString();

                if (text.length() > 1500)
                    copy.getDynamicObject()->setProperty ("content", text.substring (0, 1500)
                                                                         + "\n[... cut to keep requests small: read it again to see all of it]");
            }
            else if (type == "image" || type == "document")
            {
                // Shown once already: later requests carry a note instead of the file again.
                copy = blockOf ({ { "type", "text" }, { "text", type == "image" ? "[an image the user attached earlier]"
                                                                                 : "[a PDF the user attached earlier]" } });
            }
            else if (type == "text" && copy.getProperty ("text", {}).toString().startsWith ("Attached file "))
            {
                const auto text = copy.getProperty ("text", {}).toString();

                if (text.length() > 1500)
                    copy.getDynamicObject()->setProperty ("text", text.substring (0, 1500) + "\n[... the rest was shown earlier]");
            }

            blocks.add (copy);
        }

        result.add (makeMessage (message.getProperty ("role", {}).toString(), blocks));
    }

    return result;
}

void StellaAi::trimHistory()
{
    if (messages.size() <= maxHistory)
        return;

    // Cut only before one of the user's own messages, never between a tool call and its result.
    for (int i = messages.size() - maxHistory; i < messages.size(); ++i)
    {
        if (isPlainUserMessage (messages.getReference (i)))
        {
            messages.removeRange (0, i);
            return;
        }
    }
}
