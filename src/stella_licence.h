// C:\workspace\Stella AI Studio\src\stella_licence.h
//
// Fanan's registration in an exported plugin, compiled in when the project is protected
// (STELLA_PROTECTED). The same scheme as Grex and OnStage, which have held in the field:
//
//   - the plugin shows a Machine ID: five characters of the system drive's volume serial,
//     mapped to digits exactly as Grex's RegistrationManager does;
//   - its serial is HMAC-SHA256 (the plugin's own key, the Machine ID as ASCII), first 8
//     bytes as a big-endian number, mod 1,000,000,000, nine digits with leading zeros;
//   - a serial that matches is kept in Documents\<vendor>\<plugin>_license.key, and checked
//     against this computer again each time (a copied licence does nothing elsewhere);
//   - until then, the demo: 18 seconds of sound, then 3 seconds of silence.
//
// The key is the plugin's own, made by the studio when protection was switched on, so a
// serial for one plugin opens no other. It's in the plugin scrambled, not in plain sight:
// this stops key generators, not a determined patcher, as Grex's notes say.
//
// No JUCE: plain C++17 and, on Windows, the system's own calls.

#pragma once

#include "stella_plugin_info.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shlobj.h>
#endif

namespace stella::licence
{
    //==========================================================================
    // From the export (stella_licence_data.cpp): the key, as two halves to XOR, and the
    // "Unregistered" badge the panel shows (a PNG).
    extern const unsigned char keyA[32], keyB[32];
    extern const unsigned char badgePng[];
    extern const int badgePngSize;
    extern const int badgeX, badgeY;   // where the panel's Registration element put it (-1: bottom right)

    constexpr int serialDigits = 9;
    constexpr double demoPlaySeconds = 18.0, demoSilentSeconds = 3.0;

    //==========================================================================
    // SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104).
    namespace detail
    {
        struct Sha256
        {
            std::uint32_t h[8] { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
            unsigned char block[64] {};
            std::uint64_t length = 0;
            int used = 0;

            static std::uint32_t rotr (std::uint32_t x, int n) noexcept    { return (x >> n) | (x << (32 - n)); }

            void compress() noexcept
            {
                static const std::uint32_t k[64] {
                    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

                std::uint32_t w[64];

                for (int i = 0; i < 16; ++i)
                    w[i] = (std::uint32_t) block[i * 4] << 24 | (std::uint32_t) block[i * 4 + 1] << 16
                         | (std::uint32_t) block[i * 4 + 2] << 8 | (std::uint32_t) block[i * 4 + 3];

                for (int i = 16; i < 64; ++i)
                {
                    const auto s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
                    const auto s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];

                for (int i = 0; i < 64; ++i)
                {
                    const auto t1 = hh + (rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                    const auto t2 = (rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
                }

                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
            }

            void add (const unsigned char* data, std::size_t size) noexcept
            {
                for (std::size_t i = 0; i < size; ++i)
                {
                    block[used++] = data[i];
                    ++length;

                    if (used == 64)
                    {
                        compress();
                        used = 0;
                    }
                }
            }

            void finish (unsigned char out[32]) noexcept
            {
                const auto bits = length * 8;
                const unsigned char one = 0x80, zero = 0;
                add (&one, 1);

                while (used != 56)
                    add (&zero, 1);

                for (int i = 7; i >= 0; --i)
                {
                    const auto byte = (unsigned char) (bits >> (i * 8));
                    add (&byte, 1);
                }

                for (int i = 0; i < 8; ++i)
                    for (int j = 0; j < 4; ++j)
                        out[i * 4 + j] = (unsigned char) (h[i] >> (24 - j * 8));
            }
        };

        inline void hmacSha256 (const unsigned char* key, std::size_t keySize, const unsigned char* message, std::size_t size,
                                unsigned char out[32]) noexcept
        {
            unsigned char padded[64] {};

            if (keySize > 64)
            {
                Sha256 hashed;
                hashed.add (key, keySize);
                hashed.finish (padded);
            }
            else
            {
                std::memcpy (padded, key, keySize);
            }

            unsigned char inner[64], outer[64], innerHash[32];

            for (int i = 0; i < 64; ++i)
            {
                inner[i] = (unsigned char) (padded[i] ^ 0x36);
                outer[i] = (unsigned char) (padded[i] ^ 0x5c);
            }

            Sha256 first;
            first.add (inner, 64);
            first.add (message, size);
            first.finish (innerHash);

            Sha256 second;
            second.add (outer, 64);
            second.add (innerHash, 32);
            second.finish (out);
        }

        /** Grex's mapping from the volume serial to the Machine ID: its hex digits (upper
            case, at least five, padded on the right with zeros), the first five, each turned
            into a digit by Grex's table, read as a number. */
        inline std::string machineIdFromVolumeSerial (std::uint32_t serial)
        {
            static const char hexDigits[] = "0123456789ABCDEF";
            std::string hex;

            do
            {
                hex.insert (hex.begin(), hexDigits[serial & 15u]);
                serial >>= 4;
            }
            while (serial != 0);

            while (hex.size() < 5)
                hex += '0';

            hex.resize (5);

            std::string digits;

            for (const auto c : hex)
            {
                char value = '0';

                switch (c)
                {
                    case 'A': value = '1'; break;  case 'B': value = '2'; break;  case 'C': value = '3'; break;
                    case 'D': value = '4'; break;  case 'E': value = '5'; break;  case 'F': value = '6'; break;
                    case '1': value = '8'; break;  case '2': value = '9'; break;  case '3': value = '2'; break;
                    case '4': value = '1'; break;  case '5': value = '3'; break;  case '6': value = '4'; break;
                    case '7': value = '5'; break;  case '8': value = '6'; break;  case '9': value = '7'; break;
                    case '0': value = '8'; break;
                    default:  value = '0'; break;
                }

                digits += value;
            }

            // As a number: leading zeros go, as with juce::String::getIntValue in Grex.
            long number = 0;

            for (const auto c : digits)
                number = number * 10 + (c - '0');

            return std::to_string (number);
        }
    }

    //==========================================================================
    /** The nine digits a Machine ID's serial must be, under a key. */
    inline std::string serialFor (const unsigned char* key, std::size_t keySize, const std::string& machineId)
    {
        unsigned char mac[32];
        detail::hmacSha256 (key, keySize, reinterpret_cast<const unsigned char*> (machineId.data()), machineId.size(), mac);

        std::uint64_t value = 0;

        for (int i = 0; i < 8; ++i)
            value = (value << 8) | mac[i];

        auto text = std::to_string (value % 1000000000ull);

        while ((int) text.size() < serialDigits)
            text.insert (text.begin(), '0');

        return text;
    }

    /** This computer's Machine ID, as the user reads it off the screen. */
    inline const std::string& machineId()
    {
        static const std::string id = []
        {
           #if defined (_WIN32)
            DWORD serial = 0;

            if (GetVolumeInformationW (L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
                return detail::machineIdFromVolumeSerial ((std::uint32_t) serial);

            return std::string ("88888");   // Grex's "00000", mapped
           #else
            return std::string ("12345");
           #endif
        }();

        return id;
    }

    /** Whether a typed serial is this computer's: spaces and dashes are ignored. */
    inline bool matches (const std::string& typed)
    {
        std::string clean;

        for (const auto c : typed)
        {
            if (c >= '0' && c <= '9')
                clean += c;
            else if (c != ' ' && c != '-' && c != '\t' && c != '\r' && c != '\n')
                return false;
        }

        if (clean.empty())
            return false;

        unsigned char key[32];

        for (int i = 0; i < 32; ++i)
            key[i] = (unsigned char) (keyA[i] ^ keyB[i]);

        const auto expected = serialFor (key, sizeof (key), machineId());
        std::memset (key, 0, sizeof (key));
        return clean == expected;
    }

    //==========================================================================
   #if defined (_WIN32)
    /** Where the licence is kept: Documents\<vendor>\<plugin>_license.key. */
    inline std::wstring licenceFile()
    {
        auto legal = [] (const char* text, const wchar_t* fallback)
        {
            std::wstring result;

            for (const char* c = text; *c != 0; ++c)
            {
                const auto ch = (unsigned char) *c;

                if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == ' ' || ch == '-' || ch == '_')
                    result += (wchar_t) ch;
            }

            while (! result.empty() && result.back() == L' ')
                result.pop_back();

            return result.empty() ? std::wstring (fallback) : result;
        };

        std::wstring documents;
        PWSTR path = nullptr;

        // FOLDERID_Documents, spelled out here: the plugin then needs no uuid library.
        static const GUID documentsFolder = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };

        if (SUCCEEDED (SHGetKnownFolderPath (documentsFolder, 0, nullptr, &path)) && path != nullptr)
            documents = path;

        if (path != nullptr)
            CoTaskMemFree (path);

        if (documents.empty())
            return {};

        return documents + L"\\" + legal (stella::info::vendor, L"Plugins") + L"\\" + legal (stella::info::name, L"Plugin") + L"_license.key";
    }
   #endif

    inline std::atomic<int>& registeredFlag()
    {
        static std::atomic<int> flag { -1 };   // -1: not checked yet
        return flag;
    }

    /** Reads the licence and checks it against this computer. Message thread, at load. */
    inline void check()
    {
        if (registeredFlag().load() >= 0)
            return;

        bool ok = false;

       #if defined (_WIN32)
        const auto file = licenceFile();
        HANDLE handle = file.empty() ? INVALID_HANDLE_VALUE
                                     : CreateFileW (file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

        if (handle != INVALID_HANDLE_VALUE)
        {
            char text[64] {};
            DWORD read = 0;

            if (ReadFile (handle, text, sizeof (text) - 1, &read, nullptr))
                ok = matches (std::string (text, read));

            CloseHandle (handle);
        }
       #endif

        registeredFlag().store (ok ? 1 : 0);
    }

    inline bool isRegistered()
    {
        return registeredFlag().load (std::memory_order_relaxed) == 1;
    }

    /** A typed serial: if it's this computer's, it's kept and the plugin is registered. */
    inline bool tryRegister (const std::string& typed)
    {
        if (! matches (typed))
            return false;

       #if defined (_WIN32)
        // Kept for next time. A folder that can't be written isn't fatal: the session is
        // registered, and the serial is asked for again next time.
        const auto file = licenceFile();

        if (! file.empty())
        {
            CreateDirectoryW (file.substr (0, file.find_last_of (L'\\')).c_str(), nullptr);

            std::string digits;

            for (const auto c : typed)
                if (c >= '0' && c <= '9')
                    digits += c;

            HANDLE handle = CreateFileW (file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (handle != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile (handle, digits.data(), (DWORD) digits.size(), &written, nullptr);
                CloseHandle (handle);
            }
        }
       #endif

        registeredFlag().store (1);
        return true;
    }

    //==========================================================================
    /** The demo, on the audio thread: until the plugin is registered, 18 seconds of sound
        then 3 of silence, over and over, with short fades so it doesn't click. Counted in
        samples, so a faster-than-real-time bounce gets its silences too. One per instance. */
    class DemoGate
    {
    public:
        void process (float* left, float* right, int numFrames, double sampleRate) noexcept
        {
            const bool registered = isRegistered();

            if (registered && gain >= 1.0f)
                return;

            const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
            const auto step = 1.0 / rate;
            const auto fade = (float) (1.0 / (0.02 * rate));   // 20 ms

            for (int i = 0; i < numFrames; ++i)
            {
                const bool silent = ! registered && position >= demoPlaySeconds;
                gain = silent ? (gain - fade > 0.0f ? gain - fade : 0.0f) : (gain + fade < 1.0f ? gain + fade : 1.0f);

                if (left != nullptr)  left[i] *= gain;
                if (right != nullptr) right[i] *= gain;

                position += step;

                if (position >= demoPlaySeconds + demoSilentSeconds)
                    position -= demoPlaySeconds + demoSilentSeconds;
            }
        }

    private:
        double position = 0.0;   // seconds into the cycle
        float gain = 1.0f;
    };
}
