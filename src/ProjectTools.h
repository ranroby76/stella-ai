// C:\workspace\Stella AI Studio\src\ProjectTools.h

#pragma once

#include <juce_core/juce_core.h>

#include "LivePreview.h"
#include "Project.h"

#include <functional>

//==============================================================================
/**
    Stella AI's hands: the tools it calls to look at the project, write its modules, wire
    the graph and build it. The studio runs every call itself, on its own terms: files
    only inside the project's modules folder and graph.json, and builds through the live
    preview, so whatever the AI builds plays at once.

    Two sets of tools: in a conversation, the AI can look, design the GUI (set_layout) and
    hand over to the builder (start_building); the builder can also change the code and
    build it.
*/
class ProjectTools final
{
public:
    ProjectTools (Project& project, LivePreview& preview);

    /** The tool definitions sent with each request. */
    juce::var getDefinitions (bool builder) const;

    /** What the AI is told about the project with every request. */
    juce::String describeProject() const;

    /** The builder guide with the DSP API: the same with every request (so it's cached). */
    static const juce::String& getGuide();

    using Done = std::function<void (const juce::String& result, bool isError)>;

    /** Runs one tool call on the message thread. done is called when it's finished (for
        build, once the plugin is built and playing, or has failed). */
    void run (const juce::String& name, const juce::var& input, Done done);

    /** One short line for the chat about what a call is doing (empty: nothing to show). */
    static juce::String describeCall (const juce::String& name, const juce::var& input);

    /** Called on the message thread when the AI has changed the GUI layout. */
    std::function<void()> onLayoutChanged;

    /** Called on the message thread when the AI has saved a preset. */
    std::function<void()> onPresetsChanged;

    /** Creates and opens a new project (the studio's New, done for the AI). */
    std::function<juce::Result (const juce::String& name, PluginKind kind)> onCreateProject;

private:
    juce::Result resolve (const juce::String& path, juce::File& file, bool forWriting) const;
    juce::String buildResult (bool ok) const;

    Project& project;
    LivePreview& preview;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectTools)
};
