// C:\workspace\Stella AI Studio\src\PluginCanvasMenu.h
// The canvas's edit menu, Stella AI banner and programmed elements' bridge. Only the PluginCanvas files include this.

#pragma once

#include "ColourSwatch.h"
#include "PluginCanvas.h"
#include "stella_element_host.h"

//==============================================================================
/**
    The edit menu, the one menu a right-click opens: beside the element (or the window, for
    its empty space, or the elements selected together). Under its title what it does with
    them: Copy, Paste, Duplicate and the layers (a list of its own); then what edits it on
    the spot, its parameter typed in by id; Delete at the end. At its foot, an instruction
    for Stella AI about this very element: "make it gold and a bit bigger".
*/
class PluginCanvas::ElementMenu final : public juce::Component
{
public:
    explicit ElementMenu (PluginCanvas& owner);
    ~ElementMenu() override;

    /** An element (its index), or -1 for the window itself. */
    void showFor (int widgetIndex);

    /** Several elements selected together: what works on them all. */
    void showForSeveral (const std::vector<int>& indexes);

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
    void applySeveral();
    void applyWindow();
    void send();

    /** What the actions work on: the element, or the ones selected together. */
    std::vector<int> targets() const;
    void showActions (bool forElements);
    void layer (int choice);   // the Layer list's item

    /** A parameter typed in: its id, or a name (any case) for the id; otherwise what was typed,
        as an id, for a parameter the plugin doesn't have yet. */
    juce::String paramIdFor (const juce::String& typed) const;
    juce::String paramCaption (const juce::String& caption, const juce::String& id) const;
    int footerSize() const    { return several.empty() ? 118 : 0; }

    PluginCanvas& canvas;
    int widget = -1;
    std::vector<int> several;   // the elements selected together (empty: one, or the window)
    bool updating = false;
    juce::String title, subtitle;

    juce::TextButton closeButton;

    // What it does, under the title.
    juce::TextButton copyButton { "Copy" }, pasteButton { "Paste" }, duplicateButton { "Duplicate" };
    juce::ComboBox layerBox;
    juce::Rectangle<int> actionsArea;

    // An element's rows.
    juce::ComboBox type, source, mode, style, pictureMode, lowKey, highKey;
    juce::TextEditor param, roles[4];   // parameter ids, typed in
    juce::TextEditor label, options, textSize;
    juce::ToggleButton boldToggle { "Bold" };
    ColourSwatchButton colour { "Colour" };
    juce::TextButton pictureButton, knobStudioButton { "Design it in the Knob Studio" }, removeButton { "Delete" },
                     windowButton { "Window size and colours" };
    juce::StringArray roleNames;
    juce::StringArray lookNames;   // the Look list's entries, by item id - 1

    // The window's rows.
    juce::TextEditor widthBox, heightBox;
    ColourSwatchButton topColour { "Top colour" }, bottomColour { "Bottom colour" };
    juce::TextButton backgroundButton;

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

//==============================================================================
/**
    What the programmed elements ask of the canvas (stella_element_host.h): their
    parameters' values and ranges, their options and settings from the layout, levels to
    show, notes to play. element is the widget's index in the layout.
*/
class PluginCanvas::ElementBridge final : public stella::ui::ElementHost
{
public:
    explicit ElementBridge (PluginCanvas& owner) : canvas (owner) {}

    bool paramInfo (int element, int slot, int info, float& result) override;
    int findSlot (int element, const std::string& role) override;
    void setValue (int element, int slot, float newValue) override;
    void gesture (int element, int slot, bool starts) override;
    bool text (int element, int which, int index, const std::string& key, std::string& result) override;
    int numOptions (int element) override;
    std::uint32_t colour (int element) override;
    float level (int element, bool rms) override;
    void readScope (int element, float* destination, int numSamples) override;
    void playNote (int element, int note, float velocity) override;
    bool isNoteDown (int note) override;
    double seconds() override;

private:
    const GuiWidget* widget (int element) const;
    const PluginCanvas::Param* paramAt (int element, int slot) const;

    PluginCanvas& canvas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElementBridge)
};
