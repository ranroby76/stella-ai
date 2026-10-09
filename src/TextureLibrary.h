// C:\workspace\Stella AI Studio\src\TextureLibrary.h
// From KnobMaker (C:\workspace\knobmaker); in Stella its folders are the studio's own (see the .cpp).

/*
    TextureLibrary.h

    Where a layer's texture comes from.

    KnobMan ships its textures as bitmaps inside the application folder and
    reads whatever else you drop into its Texture directory. We can be pointed
    at any folder — including a JKnobMan install's own Texture directory — and
    the choice is remembered in a settings file between sessions. We do not copy
    those files — they are someone else's artwork, and a fixed bitmap is the
    wrong shape for this renderer anyway: LayerRender is resolution independent
    and draws the preview at display scale, so a tile would soften at exactly
    the sizes that matter.

    So the four KnobMan defaults — Checkers, Fabric, Hairline, Sand — are
    generated in code at whatever resolution is asked for, and anything else
    comes from the user's own folder, exactly as KnobMan does it.

    Every texture is returned as ARGB. Procedural ones are greyscale, because a
    Bump-mode layer only wants a height field. A user image keeps its colour so
    a Fill-mode layer can show the real wood, carbon or brushed plate.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
namespace TextureLibrary
{
    /** The folder currently being scanned — the one the user chose, or
        defaultFolder() if they have not chosen one or the chosen one has gone
        (an unplugged drive, a renamed directory). */
    juce::File userFolder();

    /** Stella: Documents/Stella AI Studio/Textures. Used until the user points
        somewhere else. */
    juce::File defaultFolder();

    /** Points the library at a folder and writes the choice to settingsFile()
        so it survives a restart. Pass an invalid or missing folder to go back
        to the default. Drops the tile cache. */
    void setUserFolder (const juce::File&);

    /** True when the folder in use came from the user rather than the default. */
    bool isUsingCustomFolder();

    /** Where the choice is kept: <user application data>/Fanan/Stella AI Studio/textures.xml */
    juce::File settingsFile();

    /** The patterns that are generated rather than loaded. */
    juce::StringArray proceduralNames();

    /** True when this name is generated. */
    bool isProcedural (const juce::String& name);

    /** Generated names first, then every image in userFolder(), sorted. */
    juce::StringArray availableNames();

    /** A w x h ARGB tile with the scale and rotation already baked in, so the
        caller can sample it 1:1 against the layer it is texturing. Cached by
        every argument. Returns an invalid image for an unknown name. */
    juce::Image image (const juce::String& name, int w, int h,
                       float scalePercent, float angleDegrees);

    /** Drops cached tiles, so the folder is read again on the next request. */
    void rescan();
}
