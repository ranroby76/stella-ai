// C:\workspace\Stella AI Studio\src\AudioEngine.h

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "WasmPlugin.h"

#include <array>
#include <atomic>
#include <functional>

//==============================================================================
/**
    A small sine synth, so the keyboard and the audio device can be tested before a
    plugin exists. Allocation-free and lock-free: everything happens on the audio thread.
*/
class TestTone final
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    /** Adds into left/right (right may be null). Notes are applied at their sample positions. */
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept;

private:
    struct Voice
    {
        int note = -1;
        double phase = 0.0, step = 0.0;
        float level = 0.0f, target = 0.0f, velocity = 0.0f;
        std::uint32_t started = 0;
    };

    void handle (const juce::MidiMessage& message) noexcept;
    void renderVoices (float* left, float* right, int start, int end) noexcept;

    static constexpr int numVoices = 12;
    std::array<Voice, numVoices> voices {};
    double sampleRate = 48000.0;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    std::uint32_t counter = 0;
};

//==============================================================================
/**
    The studio's audio side: the audio device (ASIO preferred), MIDI inputs, the
    on-screen keyboard's state and the output meters.

    It plays the plugin being built (live, in its sandbox), fed by the keyboard and MIDI
    inputs, and by the audio inputs for effects. With no plugin it plays the test tone.
*/
class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::MidiInputCallback,
                          private juce::ChangeListener
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept   { return deviceManager; }
    juce::MidiKeyboardState& getKeyboardState() noexcept    { return keyboardState; }

    /** The highest peak on an output channel since the last call (0..1, can exceed 1). */
    float takePeak (int channel) noexcept;

    /** 0..1: how much of each audio block's time the callback used. */
    double getCpuLoad() const;

    /** The device in three short lines: its name, the sample rate, the buffer
        ("Focusrite USB ASIO", "48 kHz", "128 samples · 2.7 ms"). */
    juce::StringArray describeDeviceLines() const;

    /** e.g. "ASIO · Focusrite USB ASIO · 48 kHz · 128 samples", or "No audio device". */
    juce::String describeDevice() const;

    void setTestToneEnabled (bool shouldBeOn) noexcept    { testToneOn.store (shouldBeOn); }
    bool isTestToneEnabled() const noexcept               { return testToneOn.load(); }

    /** Silences everything that's sounding (and the on-screen keyboard's held keys). */
    void allNotesOff();

    /** Message thread. Swaps the playing plugin (null: none); the old one is deleted here. */
    void setPlugin (std::unique_ptr<WasmPlugin> newPlugin);

    /** Message thread only: the plugin is only ever replaced there. */
    WasmPlugin* getPlugin() const noexcept    { return plugin.get(); }

    double getCurrentSampleRate() const noexcept  { return currentRate.load(); }
    int getCurrentBlockSize() const noexcept      { return currentBlockSize.load(); }

    /** Called on the message thread when the device or its settings change. */
    std::function<void()> onDeviceChanged;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void openDevices();
    void saveDeviceState();
    static juce::File getStateFile();

    static void raisePeak (std::atomic<float>& peak, float value) noexcept;

    juce::AudioDeviceManager deviceManager;
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;
    juce::MidiBuffer midiBuffer;

    TestTone testTone;
    std::atomic<bool> testToneOn { true };

    juce::SpinLock pluginLock;
    std::unique_ptr<WasmPlugin> plugin;
    std::array<WasmPlugin::MidiEvent, 512> pluginEvents {};
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<int> currentBlockSize { 512 };
    std::atomic<bool> silenceRequested { false };

    std::array<std::atomic<float>, 2> peaks { 0.0f, 0.0f };

    static constexpr int midiBufferBytes = 16384;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
