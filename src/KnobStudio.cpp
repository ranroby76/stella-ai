// C:\workspace\Stella AI Studio\src\KnobStudio.cpp

#include "KnobStudio.h"
#include "StellaLookAndFeel.h"

namespace
{
    constexpr int listWidth = 230, editorWidth = 340, headerHeight = 54;
}

//==============================================================================
KnobStudio::KnobStudio()
{
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, Theme::inset);

    newButton.onClick = [this] { showNewMenu(); };
    duplicateButton.onClick = [this] { if (current.isNotEmpty()) addLook (layout.styleFor (current), current); };

    uniqueButton.setTooltip ("The knob you opened gets a copy of this look, so changes affect only that knob");
    uniqueButton.onClick = [this]
    {
        if (editingWidget >= 0 && onMakeUnique != nullptr)
        {
            const auto name = onMakeUnique (editingWidget);

            if (name.isNotEmpty())
                select (name);
        }
    };

    backButton.onClick = [this] { if (onBack != nullptr) onBack(); };

    editor.onChange = [this] (const KnobStyle& style)
    {
        if (current.isEmpty())
            return;

        preview.setStyle (style);
        layout.styles[current] = style;

        if (onStyleChanged != nullptr)
            onStyleChanged (current, style);
    };

    editorViewport.setViewedComponent (&editor, false);
    editorViewport.setScrollBarsShown (true, false);

    for (auto* c : std::initializer_list<juce::Component*> { &list, &newButton, &duplicateButton, &uniqueButton, &backButton, &preview, &editorViewport })
        addAndMakeVisible (c);
}

KnobStudio::~KnobStudio()
{
    editorViewport.setViewedComponent (nullptr, false);
}

//==============================================================================
void KnobStudio::setLayout (const GuiLayout& newLayout)
{
    layout = newLayout;
    names.clear();

    for (const auto& entry : layout.styles)
        names.add (entry.first);

    // Knobs may name a KnobMaker preset directly: those show too, and become the plugin's
    // own look the moment they're changed.
    for (const auto& w : layout.widgets)
        if (w.type == GuiWidget::Type::knob)
            names.addIfNotAlreadyThere (w.style.isNotEmpty() ? w.style : juce::String ("cream"));

    list.updateContent();

    if (names.contains (current))
        select (current);
    else
        select (names.isEmpty() ? juce::String() : names[0]);
}

void KnobStudio::editKnob (int widgetIndex)
{
    editingWidget = widgetIndex;

    if (juce::isPositiveAndBelow (widgetIndex, (int) layout.widgets.size()))
    {
        const auto& w = layout.widgets[(size_t) widgetIndex];
        select (w.style.isNotEmpty() ? w.style : juce::String ("cream"));
    }

    resized();
    repaint();
}

void KnobStudio::select (const juce::String& name)
{
    current = name;

    const auto row = names.indexOf (name);
    list.selectRow (row, false, true);

    if (current.isNotEmpty())
    {
        const auto style = layout.styleFor (current);
        editor.setStyle (style);
        preview.setStyle (style);
    }

    duplicateButton.setEnabled (current.isNotEmpty());
    uniqueButton.setVisible (editingWidget >= 0 && usersOf (current) > 1);
    resized();
    repaint();
}

int KnobStudio::usersOf (const juce::String& name) const
{
    int count = 0;

    for (const auto& w : layout.widgets)
        if (w.type == GuiWidget::Type::knob && (w.style.isNotEmpty() ? w.style : juce::String ("cream")) == name)
            ++count;

    return count;
}

juce::String KnobStudio::freeName (const juce::String& base) const
{
    auto stem = base.trim().isNotEmpty() ? base.trim() : juce::String ("look");
    stem = stem.upToLastOccurrenceOf (" ", false, false).isNotEmpty() && stem.fromLastOccurrenceOf (" ", false, false).containsOnly ("0123456789")
               ? stem.upToLastOccurrenceOf (" ", false, false) : stem;

    for (int i = 2;; ++i)
    {
        const auto candidate = stem + " " + juce::String (i);

        if (! names.contains (candidate) && layout.styles.find (candidate) == layout.styles.end())
            return candidate;
    }
}

void KnobStudio::addLook (const KnobStyle& style, const juce::String& base)
{
    const auto name = freeName (base);
    layout.styles[name] = style;
    names.add (name);
    list.updateContent();

    if (onStyleChanged != nullptr)
        onStyleChanged (name, style);

    select (name);
}

void KnobStudio::showNewMenu()
{
    juce::PopupMenu menu;
    const auto presets = KnobStyle::presetNames();

    for (int i = 0; i < presets.size(); ++i)
        menu.addItem (i + 1, "From " + presets[i]);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&newButton),
                        [safeThis = juce::Component::SafePointer<KnobStudio> (this)] (int result)
                        {
                            if (safeThis != nullptr && result > 0)
                                safeThis->addLook (KnobStyle::presetByIndex (result - 1), "look");
                        });
}

//==============================================================================
int KnobStudio::getNumRows()
{
    return names.size();
}

void KnobStudio::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, names.size()))
        return;

    if (selected)
    {
        g.setColour (Theme::accent.withAlpha (0.25f));
        g.fillRect (0, 0, width, height);
    }

    const auto name = names[row];
    const auto users = usersOf (name);

    g.setColour (Theme::text);
    g.setFont (Theme::font (14.5f, selected));
    g.drawText (name, 12, 0, width - 90, height, juce::Justification::centredLeft, true);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.5f));
    g.drawText (users == 1 ? juce::String ("1 knob") : juce::String (users) + " knobs", width - 84, 0, 74, height,
                juce::Justification::centredRight, false);
}

void KnobStudio::selectedRowsChanged (int lastRowSelected)
{
    if (juce::isPositiveAndBelow (lastRowSelected, names.size()) && names[lastRowSelected] != current)
        select (names[lastRowSelected]);
}

//==============================================================================
void KnobStudio::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 8);
    header.removeFromRight (backButton.getWidth() + (uniqueButton.isVisible() ? uniqueButton.getWidth() + 10 : 0) + 10);

    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (current.isNotEmpty() ? "Look: " + current : juce::String ("No knob looks yet"), header.removeFromTop (20), juce::Justification::centredLeft, true);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (13.0f));
    const auto users = usersOf (current);
    g.drawText (current.isEmpty() ? juce::String ("Build the plugin first: its GUI brings the knob looks.")
                                  : "Used by " + (users == 1 ? juce::String ("1 knob") : juce::String (users) + " knobs")
                                        + juce::String::fromUTF8 (". Every change shows on the plugin at once."),
                header, juce::Justification::centredLeft, true);

    g.setColour (Theme::outline);
    g.fillRect (0, headerHeight, getWidth(), 1);
    g.fillRect (listWidth, headerHeight, 1, getHeight() - headerHeight);
    g.fillRect (getWidth() - editorWidth - 1, headerHeight, 1, getHeight() - headerHeight);
}

void KnobStudio::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (16, 12);

    backButton.setBounds (header.removeFromRight (150));
    header.removeFromRight (10);
    uniqueButton.setBounds (header.removeFromRight (210));

    area.removeFromTop (1);

    auto left = area.removeFromLeft (listWidth).reduced (10);
    auto buttons = left.removeFromBottom (30);
    newButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
    duplicateButton.setBounds (buttons.reduced (2, 0));
    left.removeFromBottom (8);
    list.setBounds (left);

    auto right = area.removeFromRight (editorWidth);
    editorViewport.setBounds (right.reduced (1, 0));

    // The editor works out its height from its width: size it once to learn it, then again.
    const auto editorW = right.getWidth() - editorViewport.getScrollBarThickness() - 2;
    editor.setSize (editorW, right.getHeight());
    editor.setSize (editorW, juce::jmax (right.getHeight(), editor.getRequiredHeight()));

    preview.setBounds (area.reduced (12));
}
