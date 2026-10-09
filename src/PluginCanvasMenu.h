// C:\workspace\Stella AI Studio\src\PluginCanvasMenu.h
// The canvas's edit menu and Stella AI banner. Only the PluginCanvas files include this.

#pragma once

#include "ColourSwatch.h"
#include "PluginCanvas.h"

//==============================================================================
/**
    The edit menu: it opens beside the element that was clicked (or the window, when its
    empty space was clicked) and edits it on the spot. At its foot, an instruction for
    Stella AI about this very element: "make it gold and a bit bigger".
*/
class PluginCanvas::ElementMenu final : public juce::Component
{
public:
    explicit ElementMenu (PluginCanvas& owner);
    ~ElementMenu() override;

    /** An element (its index), or -1 for the window itself. */
    void showFor (int widgetIndex);
    int getWidgetIndex() const noexcept    { return widget; }

    int getIdealHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int menuWidth = 292;

private:
    /** The rows of controls with their captions; they scroll when the menu is short of room. */
    class Rows final : public juce::Component
    {
    public:
        struct Row
        {
            juce::String caption;
            juce::Component* first = nullptr;
            juce::Component* second = nullptr;   // beside the first
            float firstShare = 0.5f;
            int height = 26;
            bool shown = true;
        };

        std::vector<Row> rows;

        int layOut (int width);    // returns the height they take
        void paint (juce::Graphics&) override;
    };

    /** The round send arrow. */
    class SendButton final : public juce::Button
    {
    public:
        SendButton() : juce::Button ("Send") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    void addRow (const juce::String& caption, juce::Component& first, juce::Component* second = nullptr, float firstShare = 1.0f, int height = 26);
    void setShown (juce::Component& first, bool shouldShow, const juce::String& caption = {});
    void apply();
    void applyWindow();
    void fillParams (juce::ComboBox& box, const juce::String& selectedId, bool withNone);
    void send();

    PluginCanvas& canvas;
    int widget = -1;
    bool updating = false;
    juce::String title, subtitle;

    juce::TextButton closeButton;

    // An element's rows.
    juce::ComboBox type, param, source, mode, style, pictureMode, lowKey, highKey;
    juce::ComboBox roles[4];
    juce::TextEditor label, options, textSize;
    juce::ToggleButton boldToggle { "Bold" };
    ColourSwatchButton colour { "Colour" };
    juce::TextButton pictureButton, knobStudioButton { "Design it in the Knob Studio" }, removeButton { "Delete" };
    juce::StringArray roleNames;
    juce::StringArray lookNames;   // the Look list's entries, by item id - 1

    // The window's rows.
    juce::TextEditor widthBox, heightBox;
    ColourSwatchButton topColour { "Top colour" }, bottomColour { "Bottom colour" };
    juce::TextButton backgroundButton, removeBackgroundButton { "Remove" };
    juce::ComboBox backgroundMode;

    Rows rows;
    juce::Viewport viewport;

    // Stella AI.
    juce::TextEditor instruction;
    SendButton sendButton;
    juce::Rectangle<int> headerArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElementMenu)
};

//==============================================================================
/** Across the top of the canvas: Stella AI at work on a request made from an edit menu,
    then its answer (questions for the user in yellow), with Open chat and a close button. */
class PluginCanvas::AiBanner final : public juce::Component,
                                     private juce::Timer
{
public:
    explicit AiBanner (PluginCanvas& owner);
    ~AiBanner() override;

    void showWorking (const juce::String& what);
    void showReply (const juce::String& reply);
    int getIdealHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::AttributedString makeText() const;

    PluginCanvas& canvas;
    juce::String text;
    bool working = false;
    int dots = 0;
    juce::TextButton chatButton { "Open chat" }, closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiBanner)
};
