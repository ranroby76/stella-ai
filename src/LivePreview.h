// C:\workspace\Stella AI Studio\src\LivePreview.h

#pragma once

#include <juce_events/juce_events.h>

#include "AudioEngine.h"

class WasmElements;

#include <atomic>
#include <map>
#include <functional>
#include <memory>
#include <vector>

//==============================================================================
/**
    The plugin being built, playing live: compiles the project, starts the result in its
    sandbox and hands it to the audio engine. Rebuilding swaps the new version in while
    playing, and parameters keep their values across rebuilds (matched by id). The GUI's
    programmed elements are built with it, into a sandbox of their own, for the canvas.

    If the plugin stops (it crashed, or got stuck), the studio carries on and says why.
*/
class LivePreview final : private juce::Timer
{
public:
    enum class State
    {
        idle,       // no project
        building,   // compiling and starting
        playing,    // live
        failed      // didn't build, didn't start, or stopped
    };

    struct Param
    {
        int index = 0;
        juce::String id, name, module, unit;
        float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
    };

    /** A module as the plugin reports it: its place in graph.json and its ports. */
    struct ModuleInfo
    {
        int index = 0;
        juce::String id, type, name;
        juce::StringArray inputs, outputs;
    };

    explicit LivePreview (AudioEngine& audioEngine);
    ~LivePreview() override;

    /** done (optional) is called on the message thread when this build has finished:
        true if it's playing, false if it failed (or a newer build replaced it). */
    void build (const juce::File& projectFolder, const juce::String& projectId,
                std::function<void (bool ok)> done = nullptr);
    void unload();

    /** The audio device changed: a new sample rate means the plugin restarts with it. */
    void deviceChanged();

    State getState() const noexcept                       { return state; }
    juce::String getStatus() const                        { return status; }
    juce::String getLog() const                           { return log; }

    const juce::Array<Param>& getParameters() const noexcept   { return params; }
    const juce::Array<ModuleInfo>& getModules() const noexcept { return modules; }

    /** Bypass by module id; kept across rebuilds while the module exists. */
    void setBypass (const juce::String& moduleId, bool bypassed);
    bool isBypassed (const juce::String& moduleId) const       { return bypassed.contains (moduleId); }

    /** The schematic's probe: a module's output port, or "plugin" for the plugin's input.
        An empty id stops watching. Kept across rebuilds. */
    void setProbe (const juce::String& moduleId, int port);
    void readProbe (float* destination, int numSamples) const;

    /** A value a module shows on the GUI (stella::Module::display). */
    struct DisplayInfo
    {
        int index = 0;
        juce::String id, module, name;
    };

    const juce::Array<DisplayInfo>& getDisplays() const noexcept   { return displays; }

    /** What the GUI's meters, lamps and scopes watch, by name: "voices.out" (a module's
        output), "plugin.out L" / "plugin.in R" (the plugin's own audio), or a display
        ("lfo.position"). Up to 32 levels and 3 scopes. Kept across rebuilds. */
    void setGuiSources (const juce::StringArray& levelSources, const juce::StringArray& scopeSources);

    /** A source's level: the highest peak since the last call (or the RMS), or a display's value. */
    float takeLevel (const juce::String& source, bool rms);
    void readScope (const juce::String& source, float* destination, int numSamples) const;
    int getParametersVersion() const noexcept             { return paramsVersion; }   // changes when the list does
    float getParameterValue (int index) const;
    void setParameterValue (int index, float value);

    /** Called on the message thread whenever the state, status or parameters change. */
    std::function<void()> onChanged;

    /** Called on the message thread with the GUI's programmed elements, each time a build
        succeeds (nullptr: the project has none), and with nullptr when it's unloaded. */
    std::function<void (std::shared_ptr<WasmElements>)> onElements;

    /** The programmed elements crashed or got stuck while running (the canvas found out):
        why, until the next build. Stella AI is told, to fix them. */
    void setElementsProblem (const juce::String& why)    { elementsProblem = why; }
    juce::String getElementsProblem() const              { return elementsProblem; }

private:
    void timerCallback() override;
    void start (std::vector<std::uint8_t> wasm, const juce::String& compilerLog, double seconds, bool compiled);
    void install (std::unique_ptr<WasmPlugin> plugin);
    void applyBypassAndProbe();
    void applyGuiSources();
    bool resolve (const juce::String& source, int& moduleIndex, int& port) const;
    void setState (State newState, const juce::String& newStatus);
    static juce::Array<Param> parseParameters (const std::string& description);

    AudioEngine& audio;

    State state = State::idle;
    juce::String status, log, elementsProblem;
    juce::Array<Param> params;
    juce::Array<ModuleInfo> modules;
    juce::Array<DisplayInfo> displays;
    juce::StringArray levelSources, scopeSources;     // what the GUI asked for
    juce::StringArray tappedSources;                  // levelSources that resolved, in tap order
    juce::StringArray bypassed;
    juce::String probeModule;
    int probePort = 0;
    int paramsVersion = 0;
    int generation = 0;            // a newer build makes older results obsolete
    std::vector<std::uint8_t> wasm;   // the playing plugin, kept to restart it on a new sample rate
    double loadedRate = 0.0;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withThreadName ("Stella build").withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePreview)
};
