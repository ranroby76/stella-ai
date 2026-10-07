// C:\workspace\Stella AI Studio\src\StyleEditorPanel.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knob editor in both apps.

/*
    StyleEditorPanel.cpp
*/

#include "StyleEditorPanel.h"
#include "UiHelpers.h"

namespace
{
    constexpr int kRowHeight    = 26;
    constexpr int kHeaderHeight = 28;
    constexpr int kLabelWidth   = 120;
}

//==============================================================================
ColourSwatchButton::ColourSwatchButton (const juce::String& name)
    : juce::Button (name)
{
}

ColourSwatchButton::~ColourSwatchButton()
{
}

void ColourSwatchButton::setColourValue (juce::Colour c, juce::NotificationType notify)
{
    if (c == colour)
        return;

    colour = c;
    repaint();

    if (notify != juce::dontSendNotification && onColourChange != nullptr)
        onColourChange();
}

void ColourSwatchButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // Checkerboard so alpha is visible — the bezel colour defaults to transparent.
    const int cell = 6;
    g.setColour (juce::Colour (0xff555555));
    g.fillRoundedRectangle (r, 3.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, 3.0f);
        g.reduceClipRegion (clip);

        g.setColour (juce::Colour (0xff777777));

        for (int y = 0; y < getHeight(); y += cell)
            for (int x = 0; x < getWidth(); x += cell)
                if (((x / cell) + (y / cell)) % 2 == 0)
                    g.fillRect (x, y, cell, cell);
    }

    g.setColour (colour);
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (juce::Colours::black.withAlpha (down ? 0.8f : (highlighted ? 0.5f : 0.3f)));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void ColourSwatchButton::clicked()
{
    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop
      | juce::ColourSelector::showSliders
      | juce::ColourSelector::showColourspace
      | juce::ColourSelector::showAlphaChannel);

    selector->setName (getName());
    selector->setCurrentColour (colour, juce::dontSendNotification);
    selector->setSize (300, 380);
    selector->addChangeListener (this);

    juce::CallOutBox::launchAsynchronously (std::move (selector),
                                            getScreenBounds(),
                                            nullptr);
}

void ColourSwatchButton::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
        setColourValue (selector->getCurrentColour(), juce::sendNotification);
}

//==============================================================================
StyleEditorPanel::StyleEditorPanel()
{
    style = KnobStyle::cream();
    buildRows();
    pushToControls();
}

//==============================================================================
void StyleEditorPanel::buildRows()
{
    auto makeLabel = [this] (const juce::String& text)
    {
        auto l = std::make_unique<juce::Label> (juce::String(), text);
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        l->setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (*l);
        return l;
    };

    for (const auto& group : KnobStyle::groupOrder())
    {
        auto* header = headers.add (new juce::Label (juce::String(), group.toUpperCase()));
        header->setJustificationType (juce::Justification::centredLeft);
        header->setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
        header->setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        addAndMakeVisible (header);

        // ---- colours -------------------------------------------------------
        for (const auto& p : KnobStyle::colourParams())
        {
            if (group != p.group)
                continue;

            Row row;
            row.group  = group;
            row.label  = makeLabel (p.name);
            row.swatch = std::make_unique<ColourSwatchButton> (p.name);
            row.swatch->onColourChange = [this] { pullFromControls(); };
            addAndMakeVisible (*row.swatch);
            rows.push_back (std::move (row));
        }

        // ---- floats --------------------------------------------------------
        for (const auto& p : KnobStyle::floatParams())
        {
            if (group != p.group)
                continue;

            Row row;
            row.group  = group;
            row.label  = makeLabel (p.name);
            row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                         juce::Slider::TextBoxRight);
            row.slider->setRange ((double) p.min, (double) p.max, (double) p.interval);
            row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
            row.slider->onValueChange = [this] { pullFromControls(); };
            addAndMakeVisible (*row.slider);
            rows.push_back (std::move (row));
        }

        // ---- ints ----------------------------------------------------------
        for (const auto& p : KnobStyle::intParams())
        {
            if (group != p.group)
                continue;

            Row row;
            row.group  = group;
            row.label  = makeLabel (p.name);
            row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                         juce::Slider::TextBoxRight);
            row.slider->setRange ((double) p.min, (double) p.max, 1.0);
            row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
            row.slider->onValueChange = [this] { pullFromControls(); };
            addAndMakeVisible (*row.slider);
            rows.push_back (std::move (row));
        }

        // ---- bools ---------------------------------------------------------
        for (const auto& p : KnobStyle::boolParams())
        {
            if (group != p.group)
                continue;

            Row row;
            row.group  = group;
            row.label  = makeLabel (p.name);
            row.toggle = std::make_unique<juce::ToggleButton> ();
            row.toggle->onClick = [this] { pullFromControls(); };
            addAndMakeVisible (*row.toggle);
            rows.push_back (std::move (row));
        }
    }

    configurePanelControls (*this);
}

//==============================================================================
void StyleEditorPanel::setStyle (const KnobStyle& s)
{
    style = s;
    pushToControls();
    repaint();
}

void StyleEditorPanel::pushToControls()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    size_t index = 0;

    for (const auto& group : KnobStyle::groupOrder())
    {
        for (const auto& p : KnobStyle::colourParams())
            if (group == p.group && index < rows.size())
                rows[index++].swatch->setColourValue (style.*(p.member), juce::dontSendNotification);

        for (const auto& p : KnobStyle::floatParams())
            if (group == p.group && index < rows.size())
                rows[index++].slider->setValue ((double) (style.*(p.member)), juce::dontSendNotification);

        for (const auto& p : KnobStyle::intParams())
            if (group == p.group && index < rows.size())
                rows[index++].slider->setValue ((double) (style.*(p.member)), juce::dontSendNotification);

        for (const auto& p : KnobStyle::boolParams())
            if (group == p.group && index < rows.size())
                rows[index++].toggle->setToggleState (style.*(p.member), juce::dontSendNotification);
    }
}

void StyleEditorPanel::pullFromControls()
{
    if (updating)
        return;

    size_t index = 0;

    for (const auto& group : KnobStyle::groupOrder())
    {
        for (const auto& p : KnobStyle::colourParams())
            if (group == p.group && index < rows.size())
                style.*(p.member) = rows[index++].swatch->getColourValue();

        for (const auto& p : KnobStyle::floatParams())
            if (group == p.group && index < rows.size())
                style.*(p.member) = (float) rows[index++].slider->getValue();

        for (const auto& p : KnobStyle::intParams())
            if (group == p.group && index < rows.size())
                style.*(p.member) = (int) rows[index++].slider->getValue();

        for (const auto& p : KnobStyle::boolParams())
            if (group == p.group && index < rows.size())
                style.*(p.member) = rows[index++].toggle->getToggleState();
    }

    if (onChange != nullptr)
        onChange (style);
}

//==============================================================================
void StyleEditorPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262629));
}

void StyleEditorPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 6);

    int headerIndex = 0;
    size_t rowIndex = 0;
    int y = area.getY();

    for (const auto& group : KnobStyle::groupOrder())
    {
        if (headerIndex < headers.size())
        {
            headers[headerIndex]->setBounds (area.getX(), y, area.getWidth(), kHeaderHeight);
            y += kHeaderHeight;
            ++headerIndex;
        }

        while (rowIndex < rows.size() && rows[rowIndex].group == group)
        {
            auto& row = rows[rowIndex];
            juce::Rectangle<int> r (area.getX(), y, area.getWidth(), kRowHeight);

            row.label->setBounds (r.removeFromLeft (kLabelWidth));

            if (row.slider != nullptr)  row.slider->setBounds (r.reduced (0, 2));
            if (row.toggle != nullptr)  row.toggle->setBounds (r.removeFromLeft (30).reduced (0, 2));
            if (row.swatch != nullptr)  row.swatch->setBounds (r.removeFromLeft (80).reduced (0, 3));

            y += kRowHeight;
            ++rowIndex;
        }

        y += 8;
    }

    requiredHeight = y - area.getY() + 12;
}
