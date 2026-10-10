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
    if (! planSteps.isEmpty() && ! planDone && lastStop == "done" && planCurrent >= planSteps.size())
    {
        planDone = true;
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

    // Nothing is sent (or charged) unless the server can take it.
    if (! server.isReady())
    {
        addEntry (Entry::Kind::notice, server.getStatus() == FananServer::Status::noInternet
                                           ? "Not sent: there's no internet connection. Check it and send again."
                                           : "Not sent: Fanan's server can't be reached right now. Please try again in a moment.");
        server.checkNow();
        notify();
        return;
    }

    if (server.getCredits() == 0)
    {
        addEntry (Entry::Kind::notice, "Not sent: you're out of Stella AI credits. Use Buy credits to get more, then send again.");
        notify();
        return;
    }

    currentRequest = shownText.trim().isNotEmpty() ? shownText.trim() : request;

    if (planDone)
        clearPlan();   // a finished plan isn't told again; an unfinished one is, so "continue" carries on

    if (onTurnStarted != nullptr)
        onTurnStarted (currentRequest);

    trimHistory();
    turnStart = messages.size();

    if (blocks.isEmpty())
    {
        messages.add (makeMessage ("user", request));
    }
    else
    {
        blocks.add (blockOf ({ { "type", "text" }, { "text", request } }));
        messages.add (makeMessage ("user", blocks));
    }

    mode = "chat";
    lastStop.clear();
    roundsLeft = maxRounds;
    stopRequested = false;
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
    body->setProperty ("context", tools.describeProject() + planContext());
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
        // Every call answered: the results go back as the next turn.
        messages.add (makeMessage ("user", toolResults));
        toolResults.clear();

        if (stopRequested)
        {
            messages.add (makeMessage ("assistant", juce::var ("(The user stopped me here.)")));
            addEntry (Entry::Kind::notice, "Stopped.");
            finish();
            return;
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
        return;
    }

    const auto call = pendingCalls.removeAndReturn (0);
    const auto name = call.getProperty ("name", {}).toString();
    const auto id = call.getProperty ("id", {}).toString();
    const auto input = call.getProperty ("input", {});

    if (name == "plan")
    {
        const auto result = runPlan (input);
        toolResults.add (makeToolResult (id, result, result.startsWith ("Not ")));
        notify();
        runNextTool();
        return;
    }

    if (name == "start_building")
    {
        // From here on, this request goes to the builder.
        mode = "build";
        addEntry (Entry::Kind::activity, juce::String::fromUTF8 ("Starting to build\xe2\x80\xa6"));
        toolResults.add (makeToolResult (id, "The builder has taken over. Go ahead: look at the project, write the modules, wire them and build.", false));
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

//==============================================================================
juce::String StellaAi::runPlan (const juce::var& input)
{
    // New steps: a new plan, with its own checklist in the conversation.
    if (const auto* list = input.getProperty ("steps", {}).getArray(); list != nullptr && ! list->isEmpty())
    {
        juce::StringArray steps;

        for (const auto& step : *list)
            if (const auto text = step.toString().replaceCharacters ("\r\n", "  ").trim(); text.isNotEmpty())
                steps.add (text.substring (0, 80));

        if (steps.isEmpty())
            return "Not done: the steps were empty.";

        if (steps.size() > maxPlanSteps)
            return "Not done: at most " + juce::String (maxPlanSteps) + " steps. Group small things into one step.";

        planSteps = steps;
        planCurrent = 0;
        planDone = false;
        planRequest = currentRequest;
        entries.push_back ({ Entry::Kind::plan, {} });
        planEntry = entries.size() - 1;
    }
    else if (planSteps.isEmpty())
    {
        return "Not done: there's no plan yet. Give its steps first.";
    }

    if (input.hasProperty ("current"))
    {
        const auto current = juce::jlimit (0, planSteps.size(), (int) input.getProperty ("current", 0));

        if (current != planCurrent && current > 0)
            addEntry (Entry::Kind::activity, "Step " + juce::String (current) + " of " + juce::String (planSteps.size()) + ": " + planSteps[current - 1]);

        planCurrent = current;
    }

    if ((bool) input.getProperty ("done", false))
    {
        planDone = true;
        planCurrent = planSteps.size();
    }

    showPlan();

    if (planDone)
        return "Plan done: all " + juce::String (planSteps.size()) + " steps.";

    return "The user sees the plan" + (planCurrent > 0 ? ", now at step " + juce::String (planCurrent) + " of " + juce::String (planSteps.size())
                                                        : juce::String (" (" + juce::String (planSteps.size()) + " steps)"))
         + ". Do the steps one at a time, calling plan with current as each starts and done: true after the last.";
}

juce::String StellaAi::planText() const
{
    juce::StringArray lines;
    lines.add (juce::String (planSteps.size()) + (planSteps.size() == 1 ? " step" : " steps") + (planDone ? juce::String (", all done") : juce::String()));

    for (int i = 0; i < planSteps.size(); ++i)
    {
        const auto number = i + 1;
        const auto mark = planDone || number < planCurrent ? juce::String::fromUTF8 ("\xe2\x9c\x93 ")    // ✓
                        : number == planCurrent           ? juce::String::fromUTF8 ("\xe2\x96\xb8 ")    // ▸
                                                          : juce::String::fromUTF8 ("\xe2\x97\x8b ");   // ○
        lines.add (mark + planSteps[i]);
    }

    return lines.joinIntoString ("\n");
}

juce::String StellaAi::planContext() const
{
    if (planSteps.isEmpty() || planDone)
        return {};

    juce::String text;
    text << "\nYour plan for the request \"" << planRequest.substring (0, 200) << "\" (the user sees it as a checklist):\n";

    for (int i = 0; i < planSteps.size(); ++i)
        text << (i + 1) << ". " << planSteps[i]
             << (i + 1 < planCurrent ? " (done)" : i + 1 == planCurrent ? " (under way)" : "") << "\n";

    text << "Carry on from the step under way (or the first one not done), one step at a time, unless the user now asks for "
            "something else: then make a new plan.\n";
    return text;
}

void StellaAi::showPlan()
{
    if (planEntry < entries.size() && entries[planEntry].kind == Entry::Kind::plan)
        entries[planEntry].text = planText();
}

void StellaAi::clearPlan()
{
    planSteps.clear();
    planCurrent = 0;
    planDone = false;
    planRequest.clear();
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
