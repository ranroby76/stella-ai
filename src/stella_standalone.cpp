// C:\workspace\Stella AI Studio\src\stella_standalone.cpp
//
// The exported plugin as a standalone Windows app: the same runtime, modules and GUI as
// the CLAP and VST3 plugins, in a window of its own, playing through the computer's audio
// (WASAPI, shared with other apps) and taking notes from MIDI keyboards (WinMM) and from
// the GUI's own keyboard.
//
// The window's menu: Audio (the output; an effect's input too), MIDI (the inputs it listens
// to) and Panic (silences every note). The choices and the parameter values are kept in
// %APPDATA%\<vendor>\<name>\standalone.txt, so it opens as it was left.
//
// An effect starts with its input muted, as standalone apps usually do: a microphone next
// to the speakers would howl. The Audio menu turns it on.
//
// Built by the studio's Export with the bundled compiler, from the same sources as the
// plugins plus this file.

#include "stella_runtime.h"
#include "stella_plugin_info.h"
#include "stella_gui.h"

#if defined (_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <xmmintrin.h>
 #define STELLA_HAS_SSE 1
#endif

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
 #define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
 #define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace
{
    //==========================================================================
    // The COM ids the app needs, spelled out so no header has to provide their storage.
    const CLSID clsidDeviceEnumerator { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
    const IID iidDeviceEnumerator     { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
    const IID iidAudioClient          { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
    const IID iidRenderClient         { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
    const IID iidCaptureClient        { 0xC8ADBD64, 0xE71E, 0x48A0, { 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17 } };
    const GUID subtypeFloat           { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
    const PROPERTYKEY keyFriendlyName { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

    constexpr int blockFrames = 512;        // the runtime's largest block
    constexpr int scopeSize = 4096;
    constexpr int inputRingFrames = 1 << 16;

    template <typename T>
    void release (T*& p)
    {
        if (p != nullptr)
        {
            p->Release();
            p = nullptr;
        }
    }

    std::wstring widen (const std::string& text)
    {
        if (text.empty())
            return {};

        const auto length = MultiByteToWideChar (CP_UTF8, 0, text.data(), (int) text.size(), nullptr, 0);
        std::wstring wide ((size_t) std::max (0, length), L'\0');
        MultiByteToWideChar (CP_UTF8, 0, text.data(), (int) text.size(), wide.data(), length);
        return wide;
    }

    std::string narrow (const std::wstring& text)
    {
        if (text.empty())
            return {};

        const auto length = WideCharToMultiByte (CP_UTF8, 0, text.data(), (int) text.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8 ((size_t) std::max (0, length), '\0');
        WideCharToMultiByte (CP_UTF8, 0, text.data(), (int) text.size(), utf8.data(), length, nullptr, nullptr);
        return utf8;
    }

    /** Tiny numbers (denormals) count as zero while the plugin runs. */
    struct NoDenormals
    {
       #if STELLA_HAS_SSE
        NoDenormals() : saved (_mm_getcsr())   { _mm_setcsr (saved | 0x8040); }
        ~NoDenormals()                         { _mm_setcsr (saved); }
        unsigned int saved;
       #endif
    };

    //==========================================================================
    /** MIDI from any number of inputs (WinMM calls back on threads of its own) to the
        audio thread. Writers take turns; the audio thread reads without waiting. */
    class MidiQueue
    {
    public:
        void push (std::uint8_t status, std::uint8_t data1, std::uint8_t data2) noexcept
        {
            while (writing.exchange (true, std::memory_order_acquire)) {}

            const auto h = head.load (std::memory_order_relaxed);
            const auto next = (h + 1) % size;

            if (next != tail.load (std::memory_order_acquire))
            {
                ring[h] = { status, data1, data2 };
                head.store (next, std::memory_order_release);
            }

            writing.store (false, std::memory_order_release);
        }

        bool pop (std::uint8_t& status, std::uint8_t& data1, std::uint8_t& data2) noexcept
        {
            const auto t = tail.load (std::memory_order_relaxed);

            if (t == head.load (std::memory_order_acquire))
                return false;

            status = ring[t].status;
            data1 = ring[t].data1;
            data2 = ring[t].data2;
            tail.store ((t + 1) % size, std::memory_order_release);
            return true;
        }

    private:
        static constexpr std::uint32_t size = 1024;

        struct Message
        {
            std::uint8_t status, data1, data2;
        };

        Message ring[size] {};
        std::atomic<std::uint32_t> head { 0 }, tail { 0 };
        std::atomic<bool> writing { false };
    };

    //==========================================================================
    struct Device
    {
        std::wstring id, name;
    };

    /** The active audio devices one way (eRender: outputs, eCapture: inputs). */
    std::vector<Device> listDevices (EDataFlow flow)
    {
        std::vector<Device> devices;
        IMMDeviceEnumerator* enumerator = nullptr;

        if (FAILED (CoCreateInstance (clsidDeviceEnumerator, nullptr, CLSCTX_ALL, iidDeviceEnumerator, (void**) &enumerator)))
            return devices;

        IMMDeviceCollection* collection = nullptr;

        if (SUCCEEDED (enumerator->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE, &collection)))
        {
            UINT count = 0;
            collection->GetCount (&count);

            for (UINT i = 0; i < count; ++i)
            {
                IMMDevice* device = nullptr;

                if (FAILED (collection->Item (i, &device)))
                    continue;

                Device d;
                LPWSTR id = nullptr;

                if (SUCCEEDED (device->GetId (&id)) && id != nullptr)
                {
                    d.id = id;
                    CoTaskMemFree (id);
                }

                IPropertyStore* properties = nullptr;

                if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &properties)))
                {
                    PROPVARIANT name;
                    PropVariantInit (&name);

                    if (SUCCEEDED (properties->GetValue (keyFriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal != nullptr)
                        d.name = name.pwszVal;

                    PropVariantClear (&name);
                    properties->Release();
                }

                if (d.name.empty())
                    d.name = L"Audio device " + std::to_wstring (i + 1);

                if (! d.id.empty())
                    devices.push_back (d);

                device->Release();
            }

            collection->Release();
        }

        enumerator->Release();
        return devices;
    }

    std::vector<std::wstring> listMidiInputs()
    {
        std::vector<std::wstring> names;

        for (UINT i = 0, n = midiInGetNumDevs(); i < n; ++i)
        {
            MIDIINCAPSW caps {};

            if (midiInGetDevCapsW (i, &caps, sizeof (caps)) == MMSYSERR_NOERROR)
                names.push_back (caps.szPname);
            else
                names.push_back (L"MIDI input " + std::to_wstring (i + 1));
        }

        return names;
    }

    //==========================================================================
    /** How the device takes (or gives) its samples. */
    struct SampleFormat
    {
        int channels = 2;
        bool isFloat = true;
        int bytes = 4;        // per sample, in memory
        int validBits = 32;
    };

    SampleFormat describe (const WAVEFORMATEX* format)
    {
        SampleFormat f;
        f.channels = std::max (1, (int) format->nChannels);
        f.bytes = std::max (1, (int) format->wBitsPerSample / 8);
        f.validBits = format->wBitsPerSample;
        f.isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;

        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX))
        {
            const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (format);
            f.isFloat = IsEqualGUID (extensible->SubFormat, subtypeFloat) != 0;

            if (extensible->Samples.wValidBitsPerSample > 0)
                f.validBits = extensible->Samples.wValidBitsPerSample;
        }

        return f;
    }

    WAVEFORMATEXTENSIBLE floatStereo (DWORD rate)
    {
        WAVEFORMATEXTENSIBLE w {};
        w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        w.Format.nChannels = 2;
        w.Format.nSamplesPerSec = rate;
        w.Format.wBitsPerSample = 32;
        w.Format.nBlockAlign = 8;
        w.Format.nAvgBytesPerSec = rate * 8;
        w.Format.cbSize = (WORD) (sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX));
        w.Samples.wValidBitsPerSample = 32;
        w.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        w.SubFormat = subtypeFloat;
        return w;
    }

    float readSample (const BYTE* p, const SampleFormat& f)
    {
        if (f.isFloat)
        {
            float v;
            std::memcpy (&v, p, sizeof (float));
            return v;
        }

        switch (f.bytes)
        {
            case 2:  { std::int16_t v; std::memcpy (&v, p, 2); return (float) v / 32768.0f; }
            case 3:  { const std::int32_t v = (std::int32_t) ((std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24); return (float) v / 2147483648.0f; }
            case 4:  { std::int32_t v; std::memcpy (&v, p, 4); return (float) v / 2147483648.0f; }
            default: return 0.0f;
        }
    }

    void writeSample (BYTE* p, const SampleFormat& f, float value)
    {
        value = std::min (1.0f, std::max (-1.0f, value));

        if (f.isFloat)
        {
            std::memcpy (p, &value, sizeof (float));
            return;
        }

        switch (f.bytes)
        {
            case 2:  { const auto v = (std::int16_t) std::lround (value * 32767.0f); std::memcpy (p, &v, 2); break; }
            case 3:  { const auto v = (std::int32_t) std::lround (value * 8388607.0f); p[0] = (BYTE) (v & 0xff); p[1] = (BYTE) ((v >> 8) & 0xff); p[2] = (BYTE) ((v >> 16) & 0xff); break; }
            case 4:  { const auto v = (std::int32_t) std::llround ((double) value * 2147483647.0); std::memcpy (p, &v, 4); break; }
            default: break;
        }
    }

    //==========================================================================
    struct LiveSource
    {
        int tap = -1, display = -1, scope = -1;
    };

    /** The plugin itself: its runtime and parameters, the audio and MIDI around it, and what
        its GUI asks for. */
    class App final : public stella::gui::Host
    {
    public:
        ~App() override
        {
            stopAudio();
            closeMidi();
        }

        bool build()
        {
            if (! runtime.build())
                return false;

            const auto count = runtime.getNumParams();
            values = std::make_unique<std::atomic<float>[]> ((size_t) std::max (1, count));

            for (int i = 0; i < count; ++i)
            {
                params.push_back (runtime.getParam (i));
                values[(size_t) i].store (params.back().def);
            }

            setUpLive();
            return true;
        }

        const std::string& getError() const noexcept    { return runtime.getError(); }

        //======================================================================
        // Settings: the devices and the parameter values, in %APPDATA%\<vendor>\<name>.
        static std::wstring settingsFile()
        {
            wchar_t appData[MAX_PATH] {};

            if (GetEnvironmentVariableW (L"APPDATA", appData, MAX_PATH) == 0)
                return {};

            auto legal = [] (std::wstring name)
            {
                for (auto& c : name)
                    if (wcschr (L"<>:\"/\\|?*", c) != nullptr || c < 32)
                        c = L'_';

                return name.empty() ? std::wstring (L"Stella") : name;
            };

            auto folder = std::wstring (appData) + L"\\" + legal (widen (stella::info::vendor));
            CreateDirectoryW (folder.c_str(), nullptr);
            folder += L"\\" + legal (widen (stella::info::name));
            CreateDirectoryW (folder.c_str(), nullptr);
            return folder + L"\\standalone.txt";
        }

        void loadSettings()
        {
            const auto path = settingsFile();
            FILE* file = path.empty() ? nullptr : _wfopen (path.c_str(), L"rb");

            if (file == nullptr)
                return;

            std::string text;
            char buffer[4096];
            size_t n = 0;

            while ((n = std::fread (buffer, 1, sizeof (buffer), file)) > 0)
                text.append (buffer, n);

            std::fclose (file);

            size_t start = 0;

            while (start < text.size())
            {
                auto end = text.find ('\n', start);

                if (end == std::string::npos)
                    end = text.size();

                auto line = text.substr (start, end - start);
                start = end + 1;

                if (! line.empty() && line.back() == '\r')
                    line.pop_back();

                const auto equals = line.find ('=');

                if (equals == std::string::npos)
                    continue;

                const auto key = line.substr (0, equals), value = line.substr (equals + 1);

                if (key == "output")           outputId = widen (value);
                else if (key == "input")       inputId = widen (value);
                else if (key == "muteInput")   inputMuted.store (value != "0");
                else if (key == "midi")
                {
                    allMidi = value == "all";
                    midiChoices.clear();

                    if (! allMidi)
                    {
                        size_t from = 0;

                        while (from <= value.size())
                        {
                            auto tab = value.find ('\t', from);

                            if (tab == std::string::npos)
                                tab = value.size();

                            if (tab > from)
                                midiChoices.push_back (widen (value.substr (from, tab - from)));

                            from = tab + 1;
                        }
                    }
                }
                else if (key.rfind ("value:", 0) == 0)
                {
                    const auto id = key.substr (6);

                    for (size_t i = 0; i < params.size(); ++i)
                        if (params[i].id == id)
                            values[i].store (std::min (params[i].max, std::max (params[i].min, (float) std::atof (value.c_str()))));
                }
            }
        }

        void saveSettings() const
        {
            const auto path = settingsFile();

            if (path.empty())
                return;

            std::string text = "stella-standalone 1\n";
            text += "output=" + narrow (outputId) + "\n";
            text += "input=" + narrow (inputId) + "\n";
            text += std::string ("muteInput=") + (inputMuted.load() ? "1" : "0") + "\n";

            std::string midi = allMidi ? "all" : "";

            if (! allMidi)
                for (size_t i = 0; i < midiChoices.size(); ++i)
                    midi += (i > 0 ? "\t" : "") + narrow (midiChoices[i]);

            text += "midi=" + midi + "\n";

            for (size_t i = 0; i < params.size(); ++i)
            {
                char value[64];
                std::snprintf (value, sizeof (value), "%.9g", (double) values[i].load (std::memory_order_relaxed));
                text += "value:" + params[i].id + "=" + value + "\n";
            }

            if (FILE* file = _wfopen (path.c_str(), L"wb"))
            {
                std::fwrite (text.data(), 1, text.size(), file);
                std::fclose (file);
            }
        }

        //======================================================================
        // Audio: an output (and for effects an input), on a thread of its own.
        bool startAudio()
        {
            stopAudio();

            running.store (true);
            started.store (0);
            audioThread = std::thread ([this] { runAudio(); });

            // It says within moments whether the device opened.
            for (int i = 0; i < 300 && started.load() == 0; ++i)
                Sleep (10);

            return started.load() == 1;
        }

        void stopAudio()
        {
            running.store (false);

            if (audioThread.joinable())
                audioThread.join();
        }

        std::wstring getProblem() const
        {
            const std::lock_guard<std::mutex> lock (problemLock);
            return problem;
        }

        bool isPlaying() const noexcept    { return playing.load(); }
        double getSampleRate() const noexcept    { return rate.load(); }

        /** Silences everything: every voice stops, every echo is cleared. */
        void panic()    { panicRequested.store (true); }

        /** An effect's input, muted (it hears silence) or heard. */
        std::atomic<bool> inputMuted { true };

        //======================================================================
        // MIDI: every input, or the chosen ones.
        void openMidi()
        {
            closeMidi();
            const auto names = listMidiInputs();

            for (UINT i = 0; i < (UINT) names.size(); ++i)
            {
                if (! allMidi && std::find (midiChoices.begin(), midiChoices.end(), names[i]) == midiChoices.end())
                    continue;

                HMIDIIN handle = nullptr;

                if (midiInOpen (&handle, i, (DWORD_PTR) &midiCallback, (DWORD_PTR) this, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
                {
                    midiInStart (handle);
                    midiInputs.push_back (handle);
                }
            }
        }

        void closeMidi()
        {
            for (auto handle : midiInputs)
            {
                midiInStop (handle);
                midiInReset (handle);
                midiInClose (handle);
            }

            midiInputs.clear();
        }

        // What the menus choose.
        std::wstring outputId, inputId;
        bool allMidi = true;
        std::vector<std::wstring> midiChoices;

        //======================================================================
        // What the GUI needs (stella::gui::Host), on the window's thread.
        int findParam (const char* id) override
        {
            for (size_t i = 0; i < params.size(); ++i)
                if (params[i].id == id)
                    return (int) i;

            return -1;
        }

        float getValue (int param) override
        {
            return param >= 0 && param < (int) params.size() ? values[(size_t) param].load (std::memory_order_relaxed) : 0.0f;
        }

        void getRange (int param, float& min, float& max, float& def, float& skew) override
        {
            if (param < 0 || param >= (int) params.size())
                return;

            min = params[(size_t) param].min;
            max = params[(size_t) param].max;
            def = params[(size_t) param].def;
            skew = params[(size_t) param].skew;
        }

        void beginEdit (int) override {}
        void endEdit (int) override {}

        void setValue (int param, float value) override
        {
            if (param >= 0 && param < (int) params.size())
                values[(size_t) param].store (std::min (params[(size_t) param].max, std::max (params[(size_t) param].min, value)),
                                              std::memory_order_relaxed);
        }

        float takeLevel (int widget, bool rms) override
        {
            if (widget < 0 || widget >= (int) live.size())
                return 0.0f;

            const auto& source = live[(size_t) widget];

            if (source.display >= 0)
                return displayValues[(size_t) source.display].load (std::memory_order_relaxed);

            if (source.tap < 0)
                return 0.0f;

            return rms ? tapRms[(size_t) source.tap].load (std::memory_order_relaxed)
                       : tapPeak[(size_t) source.tap].exchange (0.0f, std::memory_order_relaxed);
        }

        void readScope (int widget, float* destination, int numSamples) override
        {
            const auto slot = widget >= 0 && widget < (int) live.size() ? live[(size_t) widget].scope : -1;
            const auto count = std::min (numSamples, scopeSize);
            const auto write = slot >= 0 ? scopeWrite[(size_t) slot].load (std::memory_order_acquire) : 0u;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto age = (std::uint32_t) (count - i);
                destination[i] = slot >= 0 && i < count && age <= write ? scopeRing[(size_t) slot][(write - age) % (std::uint32_t) scopeSize] : 0.0f;
            }
        }

        void playNote (int note, float velocity) override
        {
            guiNotes.push (note, velocity > 0.0f ? std::min (127, std::max (1, (int) std::lround (velocity * 127.0f))) : 0);
        }

        bool isNoteDown (int note) override    { return guiNotes.isDown (note); }

    private:
        static void CALLBACK midiCallback (HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
        {
            if (message != MIM_DATA)
                return;

            const auto status = (std::uint8_t) (param1 & 0xff);

            if (status < 0x80 || status >= 0xf0)
                return;   // running status never reaches here; system messages aren't for the plugin

            reinterpret_cast<App*> (instance)->midi.push (status, (std::uint8_t) ((param1 >> 8) & 0x7f), (std::uint8_t) ((param1 >> 16) & 0x7f));
        }

        void setProblem (const std::wstring& text)
        {
            const std::lock_guard<std::mutex> lock (problemLock);
            problem = text;
        }

        IMMDevice* openDevice (IMMDeviceEnumerator* enumerator, EDataFlow flow, const std::wstring& id)
        {
            IMMDevice* device = nullptr;

            // The chosen one, or (gone, or never chosen) the system's default.
            if (! id.empty() && SUCCEEDED (enumerator->GetDevice (id.c_str(), &device)))
                return device;

            if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (flow, eConsole, &device)))
                return device;

            return nullptr;
        }

        /** Opens a device's client: float stereo at the given rate if Windows converts for us,
            else the device's own format at its own rate. */
        IAudioClient* openClient (IMMDevice* device, DWORD wantedRate, DWORD flags, REFERENCE_TIME duration, SampleFormat& format, DWORD& clientRate)
        {
            IAudioClient* client = nullptr;
            WAVEFORMATEX* mix = nullptr;

            if (FAILED (device->Activate (iidAudioClient, CLSCTX_ALL, nullptr, (void**) &client)) || FAILED (client->GetMixFormat (&mix)))
            {
                release (client);
                return nullptr;
            }

            const auto rateToUse = wantedRate > 0 ? wantedRate : mix->nSamplesPerSec;
            auto wanted = floatStereo (rateToUse);

            if (SUCCEEDED (client->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                               flags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                               duration, 0, &wanted.Format, nullptr)))
            {
                format = describe (&wanted.Format);
                clientRate = rateToUse;
                CoTaskMemFree (mix);
                return client;
            }

            // A client initializes once: a fresh one, in the device's own format.
            release (client);

            if (FAILED (device->Activate (iidAudioClient, CLSCTX_ALL, nullptr, (void**) &client))
                || FAILED (client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, mix, nullptr)))
            {
                release (client);
                CoTaskMemFree (mix);
                return nullptr;
            }

            format = describe (mix);
            clientRate = mix->nSamplesPerSec;
            CoTaskMemFree (mix);
            return client;
        }

        void runAudio()
        {
            CoInitializeEx (nullptr, COINIT_MULTITHREADED);

            // Pro Audio scheduling, where Windows offers it.
            using AvSet = HANDLE (WINAPI*) (LPCWSTR, LPDWORD);
            using AvRevert = BOOL (WINAPI*) (HANDLE);
            HMODULE avrt = LoadLibraryW (L"avrt.dll");
            HANDLE task = nullptr;
            DWORD taskIndex = 0;

            if (avrt != nullptr)
                if (auto set = reinterpret_cast<AvSet> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadCharacteristicsW"))))
                    task = set (L"Pro Audio", &taskIndex);

            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDevice* outDevice = nullptr;
            IMMDevice* inDevice = nullptr;
            IAudioClient* outClient = nullptr;
            IAudioClient* inClient = nullptr;
            IAudioRenderClient* render = nullptr;
            IAudioCaptureClient* capture = nullptr;
            HANDLE wake = CreateEventW (nullptr, FALSE, FALSE, nullptr);
            UINT32 bufferFrames = 0;
            bool ok = false;

            setProblem ({});

            do
            {
                if (FAILED (CoCreateInstance (clsidDeviceEnumerator, nullptr, CLSCTX_ALL, iidDeviceEnumerator, (void**) &enumerator)))
                {
                    setProblem (L"Windows' audio isn't available.");
                    break;
                }

                outDevice = openDevice (enumerator, eRender, outputId);

                if (outDevice == nullptr)
                {
                    setProblem (L"There's no audio output.");
                    break;
                }

                DWORD sampleRate = 0;
                outClient = openClient (outDevice, 0, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 100000, outFormat, sampleRate);

                if (outClient == nullptr || FAILED (outClient->SetEventHandle (wake)) || FAILED (outClient->GetBufferSize (&bufferFrames))
                    || FAILED (outClient->GetService (iidRenderClient, (void**) &render)))
                {
                    setProblem (L"The audio output couldn't be opened: another program may be using it on its own.");
                    break;
                }

                // An effect listens to an input too, at the output's rate.
                if (! stella::info::isInstrument)
                {
                    inDevice = openDevice (enumerator, eCapture, inputId);
                    DWORD inRate = 0;

                    if (inDevice != nullptr)
                        inClient = openClient (inDevice, sampleRate, 0, 200000, inFormat, inRate);

                    if (inClient != nullptr && (inRate != sampleRate || FAILED (inClient->GetService (iidCaptureClient, (void**) &capture))))
                    {
                        release (capture);
                        release (inClient);
                    }

                    if (inClient == nullptr)
                        setProblem (L"No audio input: the effect hears silence.");
                }

                if (! runtime.prepare ((double) sampleRate, blockFrames))
                {
                    setProblem (widen ("The plugin couldn't start: " + runtime.getError()));
                    break;
                }

                rate.store ((double) sampleRate);
                inputFill = inputRead = 0;

                // It starts silent.
                BYTE* data = nullptr;

                if (SUCCEEDED (render->GetBuffer (bufferFrames, &data)))
                    render->ReleaseBuffer (bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);

                if (inClient != nullptr)
                    inClient->Start();

                ok = SUCCEEDED (outClient->Start());

                if (! ok)
                    setProblem (L"The audio output didn't start.");
            }
            while (false);

            playing.store (ok);
            started.store (ok ? 1 : 2);

            while (ok && running.load())
            {
                WaitForSingleObject (wake, 100);

                if (! running.load())
                    break;

                if (capture != nullptr)
                    pullInput (capture, bufferFrames);

                UINT32 padding = 0;

                if (FAILED (outClient->GetCurrentPadding (&padding)))
                {
                    setProblem (L"The audio output went away. Pick one in the Audio menu.");
                    break;
                }

                const auto frames = bufferFrames > padding ? bufferFrames - padding : 0u;
                BYTE* data = nullptr;

                if (frames == 0 || FAILED (render->GetBuffer (frames, &data)))
                    continue;

                renderInto (data, frames);
                render->ReleaseBuffer (frames, 0);
            }

            playing.store (false);

            if (outClient != nullptr) outClient->Stop();
            if (inClient != nullptr) inClient->Stop();

            release (render);
            release (capture);
            release (outClient);
            release (inClient);
            release (outDevice);
            release (inDevice);
            release (enumerator);
            CloseHandle (wake);

            if (task != nullptr && avrt != nullptr)
                if (auto revert = reinterpret_cast<AvRevert> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvRevertMmThreadCharacteristics"))))
                    revert (task);

            if (avrt != nullptr)
                FreeLibrary (avrt);

            CoUninitialize();
        }

        /** An effect's input: what's arrived, into a ring the blocks read from. */
        void pullInput (IAudioCaptureClient* capture, UINT32 bufferFrames)
        {
            UINT32 packet = 0;

            while (SUCCEEDED (capture->GetNextPacketSize (&packet)) && packet > 0)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;

                if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
                    break;

                const auto frameBytes = (size_t) (inFormat.bytes * inFormat.channels);

                for (UINT32 i = 0; i < frames; ++i)
                {
                    float left = 0.0f, right = 0.0f;

                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr)
                    {
                        const auto* frame = data + (size_t) i * frameBytes;
                        left = readSample (frame, inFormat);
                        right = inFormat.channels > 1 ? readSample (frame + inFormat.bytes, inFormat) : left;
                    }

                    if (inputFill == inputRingFrames)
                    {
                        inputRead = (inputRead + 1) % inputRingFrames;   // full: the oldest goes
                        --inputFill;
                    }

                    const auto at = (inputRead + inputFill) % inputRingFrames;
                    inputRing[0][(size_t) at] = left;
                    inputRing[1][(size_t) at] = right;
                    ++inputFill;
                }

                capture->ReleaseBuffer (frames);
            }

            // Kept short: past a few buffers behind, the oldest goes.
            const auto most = (int) bufferFrames * 3;

            while (inputFill > most)
            {
                inputRead = (inputRead + 1) % inputRingFrames;
                --inputFill;
            }
        }

        void renderInto (BYTE* data, UINT32 frames)
        {
            const NoDenormals noDenormals;
            auto& shared = runtime.getShared();

            if (panicRequested.exchange (false))
            {
                int note = 0, velocity = 0;
                std::uint8_t s = 0, a = 0, b = 0;

                while (guiNotes.pop (note, velocity)) {}
                while (midi.pop (s, a, b)) {}

                runtime.reset();
                guiNotes.clear();
            }

            const auto frameBytes = (size_t) (outFormat.bytes * outFormat.channels);

            for (UINT32 done = 0; done < frames;)
            {
                const auto n = std::min<UINT32> (frames - done, (UINT32) blockFrames);
                std::uint32_t count = 0;

                // Notes from the GUI's keyboard and the MIDI inputs, at the start of the block.
                if (done == 0)
                {
                    int note = 0, velocity = 0;

                    while (guiNotes.pop (note, velocity))
                    {
                        if (count < (std::uint32_t) stella::maxEvents)
                            shared.events[count++] = { 0, (std::uint8_t) (velocity > 0 ? 0x90 : 0x80), (std::uint8_t) note, (std::uint8_t) velocity, 0 };

                        guiNotes.sounding (note, velocity > 0);
                    }

                    std::uint8_t status = 0, data1 = 0, data2 = 0;

                    while (midi.pop (status, data1, data2))
                    {
                        if (count < (std::uint32_t) stella::maxEvents)
                            shared.events[count++] = { 0, status, data1, data2, 0 };

                        const auto type = status & 0xf0;

                        if (type == 0x90 || type == 0x80)
                            guiNotes.sounding (data1, type == 0x90 && data2 > 0);
                    }
                }

                shared.numEvents = count;

                for (size_t i = 0; i < params.size(); ++i)
                    shared.params[i] = values[i].load (std::memory_order_relaxed);

                const bool muted = inputMuted.load (std::memory_order_relaxed);

                for (UINT32 i = 0; i < n; ++i)
                {
                    if (inputFill > 0)
                    {
                        shared.in[0][i] = muted ? 0.0f : inputRing[0][(size_t) inputRead];
                        shared.in[1][i] = muted ? 0.0f : inputRing[1][(size_t) inputRead];
                        inputRead = (inputRead + 1) % inputRingFrames;
                        --inputFill;
                    }
                    else
                    {
                        shared.in[0][i] = shared.in[1][i] = 0.0f;
                    }
                }

                runtime.process ((int) n);
                captureLive (n);

                // Into the device's buffer: left and right first, any other speakers silent.
                for (UINT32 i = 0; i < n; ++i)
                {
                    auto* frame = data + (size_t) (done + i) * frameBytes;
                    const auto left = shared.out[0][i], right = shared.out[1][i];

                    if (outFormat.channels == 1)
                    {
                        writeSample (frame, outFormat, (left + right) * 0.5f);
                        continue;
                    }

                    writeSample (frame, outFormat, left);
                    writeSample (frame + outFormat.bytes, outFormat, right);

                    for (int c = 2; c < outFormat.channels; ++c)
                        writeSample (frame + (size_t) c * (size_t) outFormat.bytes, outFormat, 0.0f);
                }

                done += n;
            }
        }

        void setUpLive()
        {
            auto& shared = runtime.getShared();

            for (int i = 0; i < stella::gui::numWidgets; ++i)
            {
                const auto& w = stella::gui::widgets[i];
                LiveSource source;
                std::int32_t node = 0, port = 0;

                if ((w.kind == stella::gui::Kind::meter || w.kind == stella::gui::Kind::lamp) && w.source != nullptr)
                {
                    if (w.isDisplay)
                    {
                        source.display = runtime.findDisplay (w.source);
                    }
                    else if (numTaps < stella::maxTaps && runtime.findSignal (w.source, node, port))
                    {
                        shared.taps[numTaps] = { node, port, 0.0f, 0.0f };
                        source.tap = numTaps++;
                    }
                }
                else if (w.kind == stella::gui::Kind::scope && w.source != nullptr && numScopes < stella::maxScopes
                         && runtime.findSignal (w.source, node, port))
                {
                    shared.scopeNode[numScopes] = node;
                    shared.scopePort[numScopes] = port;
                    scopeRing[(size_t) numScopes].assign ((size_t) scopeSize, 0.0f);
                    source.scope = numScopes++;
                }

                live.push_back (source);
            }

            shared.numTaps = (std::uint32_t) numTaps;
        }

        void captureLive (std::uint32_t frames)
        {
            auto& shared = runtime.getShared();

            for (int t = 0; t < numTaps; ++t)
            {
                auto held = tapPeak[(size_t) t].load (std::memory_order_relaxed);
                const auto peak = shared.taps[t].peak;

                while (peak > held && ! tapPeak[(size_t) t].compare_exchange_weak (held, peak, std::memory_order_relaxed)) {}

                tapRms[(size_t) t].store (shared.taps[t].rms, std::memory_order_relaxed);
            }

            for (int s = 0; s < numScopes; ++s)
            {
                auto write = scopeWrite[(size_t) s].load (std::memory_order_relaxed);

                for (std::uint32_t i = 0; i < frames; ++i)
                    scopeRing[(size_t) s][(write + i) % (std::uint32_t) scopeSize] = shared.scope[s][i];

                scopeWrite[(size_t) s].store (write + frames, std::memory_order_release);
            }

            for (const auto& source : live)
                if (source.display >= 0)
                    displayValues[(size_t) source.display].store (shared.displays[source.display], std::memory_order_relaxed);
        }

        stella::Runtime runtime;
        std::vector<stella::ParamInfo> params;
        std::unique_ptr<std::atomic<float>[]> values;

        stella::gui::Notes guiNotes;   // the GUI keyboard's notes, and the notes sounding
        MidiQueue midi;
        std::vector<HMIDIIN> midiInputs;

        std::thread audioThread;
        std::atomic<bool> running { false }, playing { false }, panicRequested { false };
        std::atomic<int> started { 0 };     // 0 opening, 1 playing, 2 failed
        std::atomic<double> rate { 48000.0 };
        SampleFormat outFormat, inFormat;

        std::array<std::vector<float>, 2> inputRing { std::vector<float> ((size_t) inputRingFrames), std::vector<float> ((size_t) inputRingFrames) };
        int inputFill = 0, inputRead = 0;   // the audio thread's alone

        mutable std::mutex problemLock;
        std::wstring problem;

        std::vector<LiveSource> live;
        int numTaps = 0, numScopes = 0;
        std::array<std::atomic<float>, stella::maxTaps> tapPeak {}, tapRms {};
        std::array<std::vector<float>, stella::maxScopes> scopeRing;
        std::array<std::atomic<std::uint32_t>, stella::maxScopes> scopeWrite {};
        std::array<std::atomic<float>, stella::maxDisplays> displayValues {};
    };

    //==========================================================================
    /** The app's window: the plugin's GUI under a menu bar (Audio, MIDI, Panic). */
    class MainWindow
    {
    public:
        enum : UINT
        {
            idOutputDefault = 1000,   // + 1 + n: the nth output
            idInputDefault  = 2000,   // + 1 + n: the nth input
            idMidiAll       = 3000,   // + 1 + n: the nth MIDI input
            idPanic         = 4000,
            idMuteInput     = 4001
        };

        MainWindow (App& a, HINSTANCE instance)
            : app (a)
        {
            WNDCLASSEXW wc {};
            wc.cbSize = sizeof (wc);
            wc.lpfnWndProc = proc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW (nullptr, MAKEINTRESOURCEW (32512));   // IDC_ARROW
            wc.hIcon = LoadIconW (nullptr, MAKEINTRESOURCEW (32512));       // IDI_APPLICATION
            wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
            wc.lpszClassName = L"StellaStandaloneWindow";
            RegisterClassExW (&wc);

            menu = CreateMenu();
            audioMenu = CreatePopupMenu();
            midiMenu = CreatePopupMenu();
            AppendMenuW (menu, MF_POPUP, (UINT_PTR) audioMenu, L"Audio");
            AppendMenuW (menu, MF_POPUP, (UINT_PTR) midiMenu, L"MIDI");
            AppendMenuW (menu, MF_STRING, idPanic, L"Panic");

            const bool hasGui = stella::gui::backgroundPngSize > 0;
            const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
            RECT area { 0, 0, hasGui ? stella::gui::width : 420, hasGui ? stella::gui::height : 140 };
            AdjustWindowRectEx (&area, style, TRUE, 0);

            title = widen (stella::info::name);
            hwnd = CreateWindowExW (0, wc.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                    area.right - area.left, area.bottom - area.top, nullptr, menu, instance, this);

            if (hwnd != nullptr && hasGui)
            {
                gui = stella::gui::Window::create (app);

                if (gui != nullptr)
                    gui->attach (hwnd);
            }

            if (hwnd != nullptr)
                SetTimer (hwnd, 1, 500, nullptr);
        }

        ~MainWindow()
        {
            gui.reset();

            if (hwnd != nullptr)
                DestroyWindow (hwnd);
        }

        void show (int how)
        {
            if (hwnd != nullptr)
            {
                ShowWindow (hwnd, how);
                UpdateWindow (hwnd);
            }
        }

        void say (const std::wstring& text)
        {
            MessageBoxW (hwnd, text.c_str(), title.c_str(), MB_ICONINFORMATION | MB_OK);
        }

    private:
        static LRESULT CALLBACK proc (HWND window, UINT message, WPARAM w, LPARAM l)
        {
            if (message == WM_NCCREATE)
            {
                auto* created = reinterpret_cast<CREATESTRUCTW*> (l);
                SetWindowLongPtrW (window, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (created->lpCreateParams));
            }

            auto* self = reinterpret_cast<MainWindow*> (GetWindowLongPtrW (window, GWLP_USERDATA));

            if (self == nullptr)
                return DefWindowProcW (window, message, w, l);

            switch (message)
            {
                case WM_INITMENUPOPUP:
                    if (reinterpret_cast<HMENU> (w) == self->audioMenu)  self->fillAudioMenu();
                    if (reinterpret_cast<HMENU> (w) == self->midiMenu)   self->fillMidiMenu();
                    return 0;

                case WM_COMMAND:
                    self->command (LOWORD (w));
                    return 0;

                case WM_TIMER:
                    self->updateTitle();
                    return 0;

                case WM_PAINT:
                {
                    PAINTSTRUCT paint;
                    auto* dc = BeginPaint (window, &paint);

                    if (self->gui == nullptr)
                    {
                        RECT area;
                        GetClientRect (window, &area);
                        FillRect (dc, &area, (HBRUSH) GetStockObject (BLACK_BRUSH));
                        SetBkMode (dc, TRANSPARENT);
                        SetTextColor (dc, RGB (220, 216, 206));
                        DrawTextW (dc, L"Play it with a MIDI keyboard.", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }

                    EndPaint (window, &paint);
                    return 0;
                }

                case WM_CLOSE:
                    DestroyWindow (window);
                    return 0;

                case WM_DESTROY:
                    KillTimer (window, 1);
                    self->hwnd = nullptr;
                    PostQuitMessage (0);
                    return 0;

                default:
                    break;
            }

            return DefWindowProcW (window, message, w, l);
        }

        static void clear (HMENU m)
        {
            while (GetMenuItemCount (m) > 0)
                DeleteMenu (m, 0, MF_BYPOSITION);
        }

        void fillAudioMenu()
        {
            clear (audioMenu);
            outputs = listDevices (eRender);

            AppendMenuW (audioMenu, MF_STRING | MF_GRAYED, 0, L"Output");
            AppendMenuW (audioMenu, MF_STRING | (app.outputId.empty() ? MF_CHECKED : 0u), idOutputDefault, L"    System default");

            for (size_t i = 0; i < outputs.size(); ++i)
                AppendMenuW (audioMenu, MF_STRING | (app.outputId == outputs[i].id ? MF_CHECKED : 0u), idOutputDefault + 1 + (UINT) i,
                             (L"    " + outputs[i].name).c_str());

            if (! stella::info::isInstrument)
            {
                inputs = listDevices (eCapture);
                AppendMenuW (audioMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW (audioMenu, MF_STRING | MF_GRAYED, 0, L"Input");
                AppendMenuW (audioMenu, MF_STRING | (app.inputId.empty() ? MF_CHECKED : 0u), idInputDefault, L"    System default");

                for (size_t i = 0; i < inputs.size(); ++i)
                    AppendMenuW (audioMenu, MF_STRING | (app.inputId == inputs[i].id ? MF_CHECKED : 0u), idInputDefault + 1 + (UINT) i,
                                 (L"    " + inputs[i].name).c_str());

                AppendMenuW (audioMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW (audioMenu, MF_STRING | (app.inputMuted.load() ? MF_CHECKED : 0u), idMuteInput,
                             L"Mute the input (a microphone near the speakers howls)");
            }
        }

        void fillMidiMenu()
        {
            clear (midiMenu);
            midiNames = listMidiInputs();

            AppendMenuW (midiMenu, MF_STRING | (app.allMidi ? MF_CHECKED : 0u), idMidiAll, L"Every MIDI input");
            AppendMenuW (midiMenu, MF_SEPARATOR, 0, nullptr);

            if (midiNames.empty())
                AppendMenuW (midiMenu, MF_STRING | MF_GRAYED, 0, L"No MIDI inputs");

            for (size_t i = 0; i < midiNames.size(); ++i)
            {
                const bool listening = app.allMidi || std::find (app.midiChoices.begin(), app.midiChoices.end(), midiNames[i]) != app.midiChoices.end();
                AppendMenuW (midiMenu, MF_STRING | (listening ? MF_CHECKED : 0u), idMidiAll + 1 + (UINT) i, midiNames[i].c_str());
            }
        }

        void command (UINT id)
        {
            if (id == idPanic)
            {
                app.panic();
                return;
            }

            if (id == idMuteInput)
            {
                app.inputMuted.store (! app.inputMuted.load());
                app.saveSettings();
                updateTitle();
                return;
            }

            if (id >= idOutputDefault && id <= idOutputDefault + outputs.size())
            {
                app.outputId = id == idOutputDefault ? std::wstring() : outputs[id - idOutputDefault - 1].id;
                restartAudio();
                return;
            }

            if (id >= idInputDefault && id <= idInputDefault + inputs.size())
            {
                app.inputId = id == idInputDefault ? std::wstring() : inputs[id - idInputDefault - 1].id;
                restartAudio();
                return;
            }

            if (id == idMidiAll)
            {
                app.allMidi = ! app.allMidi;

                if (! app.allMidi)
                    app.midiChoices = midiNames;   // from all to each: they stay on, one by one

                app.openMidi();
                return;
            }

            if (id > idMidiAll && id <= idMidiAll + midiNames.size())
            {
                const auto& name = midiNames[id - idMidiAll - 1];

                if (app.allMidi)
                {
                    app.allMidi = false;
                    app.midiChoices = midiNames;
                }

                const auto found = std::find (app.midiChoices.begin(), app.midiChoices.end(), name);

                if (found != app.midiChoices.end())
                    app.midiChoices.erase (found);
                else
                    app.midiChoices.push_back (name);

                app.openMidi();
            }
        }

        void restartAudio()
        {
            if (! app.startAudio())
                say (app.getProblem());

            app.saveSettings();
            updateTitle();
        }

        void updateTitle()
        {
            if (hwnd == nullptr)
                return;

            auto problem = app.getProblem();

            if (problem.empty() && ! stella::info::isInstrument && app.inputMuted.load())
                problem = L"The input is muted: the Audio menu turns it on.";

            const auto text = problem.empty() ? title : title + L"  \x2014  " + problem;

            if (text != shownTitle)
            {
                shownTitle = text;
                SetWindowTextW (hwnd, text.c_str());
            }
        }

        App& app;
        HWND hwnd = nullptr;
        HMENU menu = nullptr, audioMenu = nullptr, midiMenu = nullptr;
        std::unique_ptr<stella::gui::Window> gui;
        std::vector<Device> outputs, inputs;
        std::vector<std::wstring> midiNames;
        std::wstring title, shownTitle;
    };
}

//==============================================================================
int WINAPI WinMain (HINSTANCE instance, HINSTANCE, LPSTR, int show)
{
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);

    int result = 0;

    {
        App app;

        if (! app.build())
        {
            MessageBoxW (nullptr, widen ("It couldn't start: " + app.getError()).c_str(), widen (stella::info::name).c_str(), MB_ICONERROR | MB_OK);
            CoUninitialize();
            return 1;
        }

        app.loadSettings();

        MainWindow window (app, instance);
        window.show (show);

        app.openMidi();

        if (! app.startAudio())
            window.say (app.getProblem().empty() ? std::wstring (L"The audio output couldn't be opened. Pick one in the Audio menu.")
                                                 : app.getProblem());

        MSG message;

        while (GetMessageW (&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage (&message);
            DispatchMessageW (&message);
        }

        app.stopAudio();
        app.closeMidi();
        app.saveSettings();
        result = (int) message.wParam;
    }

    CoUninitialize();
    return result;
}

#else

int main()
{
    return 0;   // the standalone app is Windows only so far
}

#endif
