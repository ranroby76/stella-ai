// C:\workspace\Stella AI Studio\src\stella_keys.h
//
// The piano keyboard element's geometry: where each key is, which key is under the mouse
// and how hard it's struck. Shared by the studio's canvas and the exported plugin's GUI
// (plain C++17, no JUCE), so the keys sit in exactly the same place in both.

#pragma once

#include <algorithm>

namespace stella::keys
{
    inline bool isBlack (int note)
    {
        const auto n = ((note % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    /** The range a keyboard really shows: within MIDI's 0..127, starting and ending on white
        keys, and at least an octave wide. */
    inline void normalise (int& low, int& high)
    {
        low = std::min (115, std::max (0, low));
        high = std::min (127, std::max (0, high));

        if (isBlack (low))
            --low;

        if (high < low + 12)
            high = std::min (127, low + 12);

        if (isBlack (high))
            ++high;
    }

    /** The white keys from low to high (both included). */
    inline int numWhite (int low, int high)
    {
        int count = 0;

        for (int n = low; n <= high; ++n)
            count += isBlack (n) ? 0 : 1;

        return std::max (1, count);
    }

    struct Key
    {
        float x = 0, y = 0, w = 0, h = 0;
        bool black = false;

        bool contains (float px, float py) const    { return px >= x && py >= y && px < x + w && py < y + h; }
    };

    constexpr float blackWidth = 0.6f;    // of a white key's width
    constexpr float blackHeight = 0.62f;  // of the keyboard's height

    /** Where a note's key is, on a keyboard filling (x, y, w, h). A black key sits across
        the line between its two white neighbours. */
    inline Key keyOf (int note, int low, int high, float x, float y, float w, float h)
    {
        const auto whiteW = w / (float) numWhite (low, high);
        int whitesBefore = 0;

        for (int n = low; n < note; ++n)
            whitesBefore += isBlack (n) ? 0 : 1;

        if (! isBlack (note))
            return { x + (float) whitesBefore * whiteW, y, whiteW, h, false };

        const auto bw = whiteW * blackWidth;
        return { x + (float) whitesBefore * whiteW - bw * 0.5f, y, bw, h * blackHeight, true };
    }

    /** The note under a point (black keys first, as they lie on top), or -1. */
    inline int noteAt (float px, float py, int low, int high, float x, float y, float w, float h)
    {
        if (px < x || py < y || px >= x + w || py >= y + h)
            return -1;

        for (int n = low; n <= high; ++n)
            if (isBlack (n) && keyOf (n, low, high, x, y, w, h).contains (px, py))
                return n;

        const auto whiteW = w / (float) numWhite (low, high);
        const auto index = std::max (0, (int) ((px - x) / whiteW));
        int seen = 0;

        for (int n = low; n <= high; ++n)
        {
            if (isBlack (n))
                continue;

            if (seen++ == index)
                return n;
        }

        return high;
    }

    /** How hard a key is struck, 0..1: gently near its far end, hard near the front edge. */
    inline float velocityAt (float py, const Key& key)
    {
        const auto t = key.h > 0.0f ? (py - key.y) / key.h : 1.0f;
        return std::min (1.0f, std::max (0.05f, 0.35f + 0.65f * t));
    }
}
