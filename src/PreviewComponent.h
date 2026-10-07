// C:\workspace\Stella AI Studio\src\PreviewComponent.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knob editor in both apps.

/*
    PreviewComponent.h

    The live preview. One large interactive knob driven by a real juce::Slider
    through KnobLookAndFeel, plus a row showing the same style at the sizes it
    has to survive in a plugin.
*/

#pragma once

#include "KnobSource.h"

//==============================================================================
class PreviewComponent final : public juce::Component
{
public:
    PreviewComponent();
    ~PreviewComponent() override;

    void setStyle (const KnobStyle&);

    /** Draws the main knob at exactly the export frame size instead of filling
        the panel, so it can be judged at the size it will actually ship at.
        The size-comparison strip steps aside while this is on. */
    void setActualSize (bool shouldUseActualSize);
    bool isActualSize() const noexcept { return actualSize; }

    /** The pixel size 1:1 means — driven by the toolbar's Frame px. */
    void setActualSizePixels (int pixels);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Cycles dark / light / checkerboard. */
    void cycleBackground();

    float getValue() const noexcept { return (float) bigKnob.getValue(); }

private:
    void paintBackgroundInto (juce::Graphics&, juce::Rectangle<int>) const;

    ProceduralSource   source;
    KnobLookAndFeel    lookAndFeel { source };

    juce::Slider bigKnob;
    juce::Label  sizeLabel;

    juce::OwnedArray<juce::Slider> sizeKnobs;
    juce::Array<int>               sizes { 36, 64, 96 };

    int backgroundMode = 0;   // 0 dark, 1 light, 2 checker

    bool actualSize   = false;
    int  actualPixels = 96;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewComponent)
};
