// C:\workspace\Stella AI Studio\src\AudioEngine.cpp

#include "AudioEngine.h"

//==============================================================================
void TestTone::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    // One-pole envelope: about 4 ms to open, 180 ms to fade.
    attackCoeff  = (float) (1.0 - std::exp (-1.0 / (0.004 * sampleRate)));
    releaseCoeff = (float) (1.0 - std::exp (-1.0 / (0.180 * sampleRate)));

    reset();
}

void TestTone::reset() noexcept
{
    for (auto& voice : voices)
        voice = Voice();
}

void TestTone::handle (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        // A free voice, or the one playing longest.
        Voice* chosen = nullptr;

        for (auto& voice : voices)
            if (voice.note < 0)
                { chosen = &voice; break; }

        if (chosen == nullptr)
        {
            chosen = &voices[0];

            for (auto& voice : voices)
                if (voice.started < chosen->started)
                    chosen = &voice;
        }

        const auto frequency = juce::MidiMessage::getMidiNoteInHertz (message.getNoteNumber());

        chosen->note = message.getNoteNumber();
        chosen->step = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        chosen->target = 1.0f;
        chosen->velocity = message.getFloatVelocity();
        chosen->started = ++counter;
    }
    else if (message.isNoteOff())
    {
        for (auto& voice : voices)
            if (voice.note == message.getNoteNumber() && voice.target > 0.0f)
                voice.target = 0.0f;
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (auto& voice : voices)
            voice.target = 0.0f;
    }
}

void TestTone::renderVoices (float* left, float* right, int start, int end) noexcept
{
    if (end <= start)
        return;

    for (auto& voice : voices)
    {
        if (voice.note < 0)
            continue;

        const auto coeff = voice.target > voice.level ? attackCoeff : releaseCoeff;
        const auto gain = 0.18f * (0.25f + 0.75f * voice.velocity);

        for (int i = start; i < end; ++i)
        {
            voice.level += (voice.target - voice.level) * coeff;

            // A touch of the second harmonic so low notes are audible on small speakers.
            const auto sample = (float) (std::sin (voice.phase) + 0.25 * std::sin (2.0 * voice.phase)) * voice.level * gain;

            voice.phase += voice.step;

            if (voice.phase >= juce::MathConstants<double>::twoPi)
                voice.phase -= juce::MathConstants<double>::twoPi;

            left[i] += sample;

            if (right != nullptr)
                right[i] += sample;
        }

        if (voice.target == 0.0f && voice.level < 1.0e-4f)
            voice = Voice();
    }
}

void TestTone::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    int position = 0;

    for (const auto metadata : midi)
    {
        const auto when = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderVoices (left, right, position, when);
        handle (metadata.getMessage());
        position = when;
    }

    renderVoices (left, right, position, numSamples);
}

//==============================================================================
AudioEngine::AudioEngine()
{
    // The real sample rate arrives in audioDeviceAboutToStart; resetting now means MIDI
    // arriving before the device starts is still accepted.
    midiCollector.reset (48000.0);
    midiCollector.ensureStorageAllocated ((size_t) midiBufferBytes);
    midiBuffer.ensureSize ((size_t) midiBufferBytes);

    openDevices();

    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);
    deviceManager.addChangeListener (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

//==============================================================================
void AudioEngine::openDevices()
{
    const auto savedState = juce::parseXML (getStateFile());

    // Two inputs (effects being built need something to process) and a stereo output.
    deviceManager.initialise (2, 2, savedState.get(), true);

    if (savedState != nullptr)
        return;

    // First launch: prefer an installed ASIO driver, and listen to every MIDI input.
   #if JUCE_WINDOWS
    const auto previousType = deviceManager.getCurrentAudioDeviceType();

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != "ASIO")
            continue;

        if (! type->getDeviceNames().isEmpty())
        {
            deviceManager.setCurrentAudioDeviceType ("ASIO", true);

            if (deviceManager.getCurrentAudioDevice() == nullptr && previousType.isNotEmpty())
                deviceManager.setCurrentAudioDeviceType (previousType, true);
        }

        break;
    }
   #endif

    for (const auto& input : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (input.identifier, true);

    saveDeviceState();
}

juce::File AudioEngine::getStateFile()
{
    // Kept next to the executable, so a development build writes nothing outside its build folder.
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
               .getSiblingFile ("StellaAIStudio-audio.xml");
}

void AudioEngine::saveDeviceState()
{
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo (getStateFile());
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    saveDeviceState();

    if (onDeviceChanged != nullptr)
        onDeviceChanged();
}

//==============================================================================
juce::String AudioEngine::describeDevice() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio device";

    const auto rate = device->getCurrentSampleRate();
    const auto rateText = std::abs (rate - std::round (rate / 1000.0) * 1000.0) < 1.0
                              ? juce::String (juce::roundToInt (rate / 1000.0)) + " kHz"
                              : juce::String (rate / 1000.0, 1) + " kHz";

    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    return device->getTypeName() + dot + device->getName()
         + dot + rateText
         + dot + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
}

juce::StringArray AudioEngine::describeDeviceLines() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return { "No audio device", "Click to choose one", "" };

    const auto rate = device->getCurrentSampleRate();
    const auto rateText = std::abs (rate - std::round (rate / 1000.0) * 1000.0) < 1.0
                              ? juce::String (juce::roundToInt (rate / 1000.0)) + " kHz"
                              : juce::String (rate / 1000.0, 1) + " kHz";

    const auto buffer = device->getCurrentBufferSizeSamples();
    const auto milliseconds = rate > 0.0 ? 1000.0 * (double) buffer / rate : 0.0;

    return { device->getName(), rateText,
             juce::String (buffer) + juce::String::fromUTF8 (" samples \xc2\xb7 ") + juce::String (milliseconds, 1) + " ms" };
}

double AudioEngine::getCpuLoad() const
{
    return deviceManager.getCpuUsage();
}

float AudioEngine::takePeak (int channel) noexcept
{
    if (! juce::isPositiveAndBelow (channel, (int) peaks.size()))
        return 0.0f;

    return peaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
}

void AudioEngine::raisePeak (std::atomic<float>& peak, float value) noexcept
{
    auto current = peak.load (std::memory_order_relaxed);

    while (value > current && ! peak.compare_exchange_weak (current, value, std::memory_order_relaxed))
    {
    }
}

void AudioEngine::allNotesOff()
{
    keyboardState.allNotesOff (0);
    panicRequested.store (true);
    silenceRequested.store (true);
}

void AudioEngine::setPlugin (std::unique_ptr<WasmPlugin> newPlugin)
{
    std::unique_ptr<WasmPlugin> old;

    {
        // Waits at most for the audio block that's using the old plugin.
        const juce::SpinLock::ScopedLockType lock (pluginLock);
        old = std::move (plugin);
        plugin = std::move (newPlugin);
    }

    // The old plugin goes here, on the message thread, never on the audio thread.
}

//==============================================================================
void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    midiCollector.addMessageToQueue (message);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto rate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;

    currentRate.store (rate);
    currentBlockSize.store (device != nullptr ? juce::jmax (16, device->getCurrentBufferSizeSamples()) : 512);

    midiCollector.reset (rate);
    testTone.prepare (rate);

    for (auto& peak : peaks)
        peak.store (0.0f);
}

void AudioEngine::audioDeviceStopped()
{
    testTone.reset();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    // Hardware MIDI first, then the on-screen keyboard (which also lights up for hardware notes).
    midiBuffer.clear();
    midiCollector.removeNextBlockOfMessages (midiBuffer, numSamples);
    keyboardState.processNextMidiBuffer (midiBuffer, 0, numSamples, true);

    if (silenceRequested.exchange (false))
        testTone.reset();

    float* left = numOutputChannels > 0 ? outputChannelData[0] : nullptr;
    float* right = numOutputChannels > 1 ? outputChannelData[1] : nullptr;

    if (left == nullptr && right != nullptr)
        std::swap (left, right);

    bool pluginPlayed = false;

    {
        // If the plugin is being swapped right now, this block simply skips it.
        const juce::SpinLock::ScopedTryLockType lock (pluginLock);

        if (lock.isLocked() && plugin != nullptr)
        {
            int numEvents = 0;

            // Panic: "all notes off" and "all sound off" first, so every voice lets go.
            if (panicRequested.exchange (false))
            {
                pluginEvents[(size_t) numEvents++] = { 0, 0xb0, 123, 0 };
                pluginEvents[(size_t) numEvents++] = { 0, 0xb0, 120, 0 };
            }

            for (const auto metadata : midiBuffer)
            {
                const auto* raw = metadata.data;

                if (metadata.numBytes < 1 || raw[0] >= 0xf0 || numEvents >= (int) pluginEvents.size())
                    continue;

                pluginEvents[(size_t) numEvents++] = { (std::uint32_t) juce::jlimit (0, numSamples - 1, metadata.samplePosition),
                                                       raw[0],
                                                       metadata.numBytes > 1 ? raw[1] : (std::uint8_t) 0,
                                                       metadata.numBytes > 2 ? raw[2] : (std::uint8_t) 0 };
            }

            const float* inL = numInputChannels > 0 ? inputChannelData[0] : nullptr;
            const float* inR = numInputChannels > 1 ? inputChannelData[1] : inL;

            plugin->process (inL, inR, left, right, numSamples, pluginEvents.data(), numEvents);
            pluginPlayed = true;
        }
    }

    if (! pluginPlayed && left != nullptr && testToneOn.load (std::memory_order_relaxed))
        testTone.render (left, right, numSamples, midiBuffer);

    for (int channel = 0; channel < juce::jmin (2, numOutputChannels); ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        const auto range = juce::FloatVectorOperations::findMinAndMax (outputChannelData[channel], numSamples);
        raisePeak (peaks[(size_t) channel], juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())));
    }
}
