// C:\workspace\Stella AI Studio\src\UiHelpers.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knob editor in both apps.

/*
    UiHelpers.h

    Small shared UI utilities. Header-only, so nothing to add to CMakeLists.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** How many pixels of mouse travel it takes to drag a slider across its whole
    range. One value for the whole app, exposed in the toolbar.

    A function-local static in an inline function gives one instance across
    every translation unit. */
inline int& sliderSensitivityValue() noexcept
{
    static int value = 400;
    return value;
}

inline int getSliderSensitivity() noexcept
{
    return sliderSensitivityValue();
}

inline void setSliderSensitivity (int pixelsForFullRange) noexcept
{
    sliderSensitivityValue() = juce::jlimit (50, 4000, pixelsForFullRange);
}

//==============================================================================
/** Tag a slider with this and configurePanelControls leaves its drag behaviour
    alone, so it goes on tracking the pointer's position along its own track.
    For the ones where clicking a spot and landing on that spot IS the point —
    a transport scrubber, or the control that sets the sensitivity itself. */
inline void keepPointerTracking (juce::Slider& slider)
{
    slider.getProperties().set ("pointerTracking", true);
}

//==============================================================================
/** Hides the pointer while a slider is being dragged, and drops it back onto
    the handle when the button comes up.

    Uses unbounded mouse movement rather than just an invisible cursor, which
    buys a second thing worth having: the drag is no longer stopped by the edge
    of the screen. At 400 pixels for a full range that matters — a full sweep
    can easily need more travel than there is monitor.

    JUCE turns unbounded mode off on release and restores the pointer to where
    the drag began. Component mouse listeners run after the component's own
    mouseUp, so by the time this fires that restore has already happened and the
    move to the handle lands on top of it. */
class SliderDragCursor final : public juce::MouseListener
{
public:
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging != nullptr)
            return;

        auto* slider = dynamic_cast<juce::Slider*> (e.eventComponent);

        if (slider == nullptr)
            return;

        // Not for the ones you aim rather than turn — a transport scrubber or
        // the sensitivity box. There the pointer IS the control.
        if ((bool) slider->getProperties().getWithDefault ("pointerTracking", false))
            return;

        // A click is not a drag. Waiting for real movement stops the cursor
        // blinking out on every stray press.
        if (e.getDistanceFromDragStart() < 3)
            return;

        dragging = slider;
        e.source.enableUnboundedMouseMovement (true, false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* slider = dragging.getComponent();
        dragging = nullptr;

        if (slider == nullptr)
            return;

        const auto bounds = slider->getLocalBounds().toFloat();

        juce::Point<float> handle = bounds.getCentre();

        if (slider->isHorizontal())
            handle = { slider->getPositionOfValue (slider->getValue()), bounds.getCentreY() };
        else if (slider->isVertical())
            handle = { bounds.getCentreX(), slider->getPositionOfValue (slider->getValue()) };

        // e.source is const here, and setScreenPosition is not. The source is a
        // lightweight handle, so a copy addresses the same device.
        auto source = e.source;
        source.setScreenPosition (slider->localPointToGlobal (handle));
    }

private:
    juce::Component::SafePointer<juce::Slider> dragging;
};

/** One instance for the whole app. Components own their listener lists, so a
    slider being destroyed unhooks itself; this outlives all of them. */
inline SliderDragCursor& sliderDragCursor()
{
    static SliderDragCursor instance;
    return instance;
}

//==============================================================================
/** Applies the panel-control rules to a whole component tree.

    Wheel: sliders and combo boxes stop eating it. By default a Slider treats
    the wheel as a value nudge, which in a scrolling panel is wrong twice over —
    the panel does not scroll, and a value the user never meant to touch quietly
    changes. With it off, Slider::mouseWheelMove falls through to
    Component::mouseWheelMove, which hands the event to the parent, so the wheel
    reaches the Viewport and scrolls it.

    Drag: relative, scaled by the sensitivity. Getting here needs two calls, and
    only both together do anything:

      - setSliderSnapsToMousePosition(false) switches a linear slider off the
        branch that maps the pointer straight onto its own on-screen track. In
        the default snapping branch the widget's width IS the sensitivity, so a
        200px row spanning -180..180 is about two degrees per pixel and no
        setting can help it.
      - setMouseDragSensitivity(n) is then honoured: dragging n pixels moves the
        value across its full range, measured from where the drag began.

    Straight-line arithmetic, no acceleration, no threshold. Velocity mode was
    the wrong answer here — it ignores the sensitivity entirely and runs the
    movement through a sine curve with a dead zone at the bottom, which is what
    made small drags do nothing and quick ones jump a fifth of the range in one
    event.

    Cost of snapping being off: clicking the track no longer teleports the thumb
    to that spot. For an editing panel that is a gain, not a loss.

    Call it at the end of a panel's constructor, and again when the sensitivity
    changes; it walks the whole tree, so rows built by helper functions are
    covered without having to remember each one.

    Deliberately NOT applied to the preview knobs: those are the thing being
    designed, not a menu. */
inline void configurePanelControls (juce::Component& parent)
{
    const int sensitivity = getSliderSensitivity();

    for (auto* child : parent.getChildren())
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            slider->setScrollWheelEnabled (false);

            // JUCE ignores a listener it already holds, so calling this again
            // on every sensitivity change costs nothing.
            slider->addMouseListener (&sliderDragCursor(), false);

            if (! (bool) slider->getProperties().getWithDefault ("pointerTracking", false))
            {
                slider->setVelocityBasedMode (false);
                slider->setSliderSnapsToMousePosition (false);
                slider->setMouseDragSensitivity (sensitivity);
            }
        }
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
        {
            combo->setScrollWheelEnabled (false);
        }

        configurePanelControls (*child);
    }
}
