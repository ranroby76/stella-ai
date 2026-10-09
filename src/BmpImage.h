// C:\workspace\Stella AI Studio\src\BmpImage.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    BmpImage.h

    A BMP decoder, because JUCE does not have one.

    juce::ImageFileFormat registers exactly three decoders — PNG, JPEG and GIF —
    and there is no hook to add a fourth, so this cannot be plugged into
    ImageFileFormat::loadFrom. loadImageFile() below is the entry point to use
    instead: it tries JUCE first and falls back to here.

    That matters because KnobMan's own stock textures are bitmaps. Without this,
    pointing the texture folder at a JKnobMan install lists a set of files that
    all silently fail to load.

    Covers what a texture is realistically saved as: 1, 4, 8, 16, 24 and 32 bit,
    BI_RGB and BI_BITFIELDS, bottom-up or top-down, BITMAPCOREHEADER through
    BITMAPV5HEADER. RLE-compressed bitmaps are rejected rather than guessed at.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
namespace BmpImage
{
    /** Cheap magic-number test. Does not read the whole file. */
    bool looksLikeBmp (const juce::File&);

    /** An invalid image if the data is not a BMP we can decode. */
    juce::Image load (const void* data, size_t numBytes);

    juce::Image loadFrom (const juce::File&);
}

//==============================================================================
/** Every format JUCE knows, plus BMP. Use this anywhere an image comes from a
    file the user chose, rather than juce::ImageFileFormat::loadFrom. */
juce::Image loadImageFile (const juce::File&);
