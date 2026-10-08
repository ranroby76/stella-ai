// C:\workspace\Stella AI Studio\src\TopBar.cpp

#include "TopBar.h"
#include "StellaLookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "StellaAssets.h"

namespace
{
    enum MenuIds
    {
        revealId = 1,
        closeId  = 2,
        newId = 5,
        openId = 6,
        recentBaseId = 100
    };

    juce::Font tabFont()     { return Theme::font (14.5f, true); }
    juce::Font menuFont()    { return Theme::font (14.5f); }
    juce::Font pillFont()    { return Theme::font (13.0f); }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return (int) std::ceil (glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true).getWidth());
    }

    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    constexpr int pillPadding = 14, minPillWidth = 120, maxPillWidth = 460;
    constexpr int wordmarkWidth = 172, meterWidth = 16, loadWidth = 72;
}

//==============================================================================
void TopBar::MenuTitle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (open || highlighted || down)
    {
        g.setColour (open || down ? Theme::raised.brighter (0.12f) : Theme::raised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }

    g.setColour (Theme::text);
    g.setFont (menuFont());
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

void TopBar::TabButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    const bool on = getToggleState();

    if (! on && (highlighted || down))
    {
        g.setColour (Theme::raised.withAlpha (0.7f));
        g.fillRoundedRectangle (area.reduced (2.0f, 8.0f), 5.0f);
    }

    g.setColour (on ? Theme::text : (highlighted ? Theme::text.withAlpha (0.85f) : Theme::muted));
    g.setFont (Theme::font (14.5f, on));
    g.drawText (getButtonText(), area, juce::Justification::centred, false);

    // The tab that's showing carries the accent as an underline, on the bar's bottom edge.
    if (on)
    {
        g.setColour (Theme::accent);
        g.fillRect (area.withTop (area.getBottom() - 2.0f).reduced (10.0f, 0.0f));
    }
}

juce::String TopBar::DevicePill::textFor (int room) const
{
    // As many whole pieces as fit; the first one shortened only when even it doesn't.
    auto shown = pieces.isEmpty() ? juce::String() : pieces[0];

    for (int i = 2; i <= pieces.size(); ++i)
    {
        const auto longer = juce::StringArray (pieces.begin(), i).joinIntoString (dot);

        if (textWidth (pillFont(), longer) > room)
            break;

        shown = longer;
    }

    return shown;
}

int TopBar::DevicePill::getWidthFor (int room) const
{
    return juce::jmin (room, textWidth (pillFont(), textFor (room - pillPadding * 2)) + pillPadding * 2);
}

void TopBar::DevicePill::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = box.getHeight() * 0.5f;

    g.setColour (down ? Theme::raised.brighter (0.12f) : (highlighted ? Theme::raised.brighter (0.06f) : Theme::raised));
    g.fillRoundedRectangle (box, radius);
    g.setColour (highlighted ? Theme::accent.withAlpha (0.6f) : Theme::outline);
    g.drawRoundedRectangle (box, radius, 1.0f);

    g.setColour (Theme::text.withAlpha (0.88f));
    g.setFont (pillFont());
    g.drawText (textFor (getWidth() - pillPadding * 2), getLocalBounds().reduced (pillPadding, 0), juce::Justification::centred, true);
}

//==============================================================================
float TopBar::Meter::toPosition (float gain) noexcept
{
    // -60 dB at the bottom, +6 dB at the top.
    const auto db = juce::Decibels::gainToDecibels (gain, -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
}

void TopBar::Meter::setLevels (float left, float right)
{
    const float incoming[2] { left, right };

    for (int i = 0; i < 2; ++i)
    {
        // Fast up, slow down; the peak line holds for about a second.
        levels[i] = incoming[i] > levels[i] ? incoming[i] : levels[i] * 0.86f;

        if (incoming[i] >= holds[i])
        {
            holds[i] = incoming[i];
            holdFrames[i] = 30;
        }
        else if (--holdFrames[i] <= 0)
        {
            holds[i] *= 0.9f;
        }
    }

    repaint();
}

void TopBar::Meter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto barWidth = (bounds.getWidth() - 2.0f) * 0.5f;

    for (int i = 0; i < 2; ++i)
    {
        const auto frame = juce::Rectangle<float> (bounds.getX() + (float) i * (barWidth + 2.0f), bounds.getY(),
                                                   barWidth, bounds.getHeight());

        // A thin frame, so the meter shows even in silence.
        g.setColour (Theme::outline);
        g.fillRect (frame);
        const auto bar = frame.reduced (1.0f);
        g.setColour (Theme::inset);
        g.fillRect (bar);

        const auto top = bar.getBottom() - bar.getHeight() * toPosition (levels[i]);
        const auto zeroDb = bar.getBottom() - bar.getHeight() * toPosition (1.0f);
        const auto minus6 = bar.getBottom() - bar.getHeight() * toPosition (0.5f);

        g.setColour (Theme::safe);
        g.fillRect (bar.withTop (juce::jmax (top, minus6)));

        if (top < minus6)
        {
            g.setColour (Theme::hot);
            g.fillRect (bar.withTop (juce::jmax (top, zeroDb)).withBottom (minus6));
        }

        if (top < zeroDb)
        {
            g.setColour (Theme::clip);
            g.fillRect (bar.withTop (top).withBottom (zeroDb));
        }

        if (holds[i] > 0.001f)
        {
            const auto holdY = bar.getBottom() - bar.getHeight() * toPosition (holds[i]);
            g.setColour (holds[i] >= 1.0f ? Theme::clip : Theme::text.withAlpha (0.7f));
            g.fillRect (bar.withTop (holdY).withHeight (1.5f));
        }
    }
}

//==============================================================================
TopBar::TopBar()
{
    optionsMenu.setTooltip ("New, open and recent projects");
    optionsMenu.onClick = [this] { showOptionsMenu(); };

    devicePill.onClick = [this] { if (onAudioSettings != nullptr) onAudioSettings(); };
    meter.setTooltip ("Output level");

    for (auto* c : std::initializer_list<juce::Component*> { &optionsMenu, &devicePill, &meter })
        addAndMakeVisible (c);

    for (int i = 0; i < numTabs; ++i)
    {
        auto* tab = tabs.add (new TabButton (tabNames[i]));
        tab->onClick = [this, i] { if (onTabChosen != nullptr) onTabChosen (i); };
        addAndMakeVisible (tab);
    }

    setCurrentTab (0);
    setDevice ({ "No audio device" }, "No audio device is open");

    fananLogo = juce::ImageCache::getFromMemory (StellaAssets::fanan_logo_png, StellaAssets::fanan_logo_pngSize);

    startTimerHz (30);
}

TopBar::~TopBar()
{
    stopTimer();
}

//==============================================================================
void TopBar::setProjectName (const juce::String& name)
{
    hasProject = name.isNotEmpty();
}

void TopBar::setCurrentTab (int index)
{
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setToggleState (i == index, juce::dontSendNotification);
}

void TopBar::setDevice (const juce::StringArray& details, const juce::String& tooltip)
{
    juce::StringArray pieces (details);
    pieces.removeEmptyStrings();

    devicePill.setPieces (pieces);
    devicePill.setTooltip (tooltip + "\nClick for the audio and MIDI settings");
    resized();
}

void TopBar::timerCallback()
{
    meter.setLevels (takePeak != nullptr ? takePeak (0) : 0.0f, takePeak != nullptr ? takePeak (1) : 0.0f);

    const auto load = juce::String (juce::roundToInt ((audioLoad != nullptr ? audioLoad() : 0.0) * 100.0)) + "%";

    if (load != loadText)
    {
        loadText = load;
        repaint (loadArea);
    }
}

//==============================================================================
void TopBar::showOptionsMenu()
{
    // A plain Windows-style menu: text items, separators, and a submenu for recent projects.
    // Projects live on this computer and save themselves as they change.
    const auto recent = recentProjects != nullptr ? recentProjects() : juce::StringArray();

    juce::PopupMenu menu;
    menu.addItem (newId, juce::String::fromUTF8 ("New plugin\xe2\x80\xa6"));
    menu.addItem (openId, juce::String::fromUTF8 ("Open\xe2\x80\xa6"));

    juce::PopupMenu recentMenu;

    for (int i = 0; i < recent.size(); ++i)
        recentMenu.addItem (recentBaseId + i, juce::File (recent[i]).getParentDirectory().getFileName());

    menu.addSubMenu ("Open recent", recentMenu, ! recent.isEmpty());
    menu.addSeparator();
    menu.addItem (revealId, "Show project folder", hasProject);
    menu.addItem (closeId, "Close project", hasProject);

    optionsMenu.setOpen (true);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsMenu).withMinimumWidth (220),
                        [safeThis = juce::Component::SafePointer<TopBar> (this), recent] (int result)
                        {
                            if (safeThis == nullptr)
                                return;

                            safeThis->optionsMenu.setOpen (false);

                            if (result == newId && safeThis->onNew != nullptr)
                                safeThis->onNew();
                            else if (result == openId && safeThis->onOpen != nullptr)
                                safeThis->onOpen();
                            else if (result == revealId && safeThis->onRevealProject != nullptr)
                                safeThis->onRevealProject();
                            else if (result == closeId && safeThis->onCloseProject != nullptr)
                                safeThis->onCloseProject();
                            else if (result >= recentBaseId && result - recentBaseId < recent.size() && safeThis->onOpenRecent != nullptr)
                                safeThis->onOpenRecent (juce::File (recent[result - recentBaseId]));
                        });
}

//==============================================================================
void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::window);

    g.setColour (Theme::outline);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // STELLA in the accent, AI STUDIO quieter.
    if (! wordmarkArea.isEmpty())
    {
        const auto bold = Theme::font (17.0f, true);
        const auto area = wordmarkArea.toFloat();

        g.setFont (bold);
        g.setColour (Theme::accent);
        g.drawText ("STELLA", area, juce::Justification::centredLeft, false);

        g.setFont (Theme::font (17.0f));
        g.setColour (Theme::muted);
        g.drawText ("AI STUDIO", area.withTrimmedLeft ((float) textWidth (bold, "STELLA") + 6.0f), juce::Justification::centredLeft, false);
    }

    // The audio load, beside the meter.
    if (! loadArea.isEmpty())
    {
        auto load = loadArea;
        g.setColour (Theme::muted);
        g.setFont (Theme::font (10.5f));
        g.drawText ("AUDIO LOAD", load.removeFromTop (load.getHeight() / 2), juce::Justification::bottomLeft, false);
        g.setColour (Theme::text);
        g.setFont (Theme::monoFont (13.0f));
        g.drawText (loadText, load, juce::Justification::topLeft, false);
    }

    // The Fanan logo, right-aligned, as tall as the bar allows.
    if (fananLogo.isValid() && ! fananArea.isEmpty())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageWithin (fananLogo, fananArea.getX(), fananArea.getY(), fananArea.getWidth(), fananArea.getHeight(),
                           juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid | juce::RectanglePlacement::onlyReduceInSize);
    }
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    area.removeFromBottom (1);

    const auto optionsWidth = textWidth (menuFont(), optionsMenu.getButtonText()) + 22;
    const auto logoWidth = fananLogo.isValid() ? juce::roundToInt ((float) area.getHeight() * (float) fananLogo.getWidth() / (float) fananLogo.getHeight()) : 0;

    juce::Array<int> tabWidths;
    int tabsWidth = 0;

    for (auto* tab : tabs)
    {
        tabWidths.add (textWidth (tabFont(), tab->getButtonText()) + 40);
        tabsWidth += tabWidths.getLast();
    }

    // Always there: Options, the tabs and the meter, with room for a short device pill.
    // When the window is too narrow for the rest, the wordmark goes first, then the Fanan
    // logo, then the audio load.
    bool showWordmark = true, showLogo = logoWidth > 0, showLoad = true;

    const auto needed = [&]
    {
        return (showWordmark ? wordmarkWidth : 0) + optionsWidth + 12 + minPillWidth + 24 + tabsWidth + 24
             + meterWidth + (showLoad ? 8 + loadWidth : 0) + (showLogo ? 20 + logoWidth : 0);
    };

    if (needed() > area.getWidth())   showWordmark = false;
    if (needed() > area.getWidth())   showLogo = false;
    if (needed() > area.getWidth())   showLoad = false;

    wordmarkArea = showWordmark ? area.removeFromLeft (wordmarkWidth) : juce::Rectangle<int>();

    // Right: the Fanan logo (433 x 171, fitted to the bar's height), the load and the meter.
    fananArea = showLogo ? area.removeFromRight (logoWidth) : juce::Rectangle<int>();

    if (showLogo)
        area.removeFromRight (20);

    loadArea = showLoad ? area.removeFromRight (loadWidth) : juce::Rectangle<int>();

    if (showLoad)
        area.removeFromRight (8);

    meter.setBounds (area.removeFromRight (meterWidth).reduced (0, 1));
    area.removeFromRight (24);

    // Left: Options, then the device pill.
    optionsMenu.setBounds (area.removeFromLeft (optionsWidth));
    area.removeFromLeft (12);

    // The tabs: centred in the window, moving right only as far as a short pill needs.
    const auto earliest = area.getX() + minPillWidth + 24;
    const auto latest = juce::jmax (earliest, area.getRight() - tabsWidth);
    auto x = juce::jlimit (earliest, latest, (getWidth() - tabsWidth) / 2);

    devicePill.setBounds (area.removeFromLeft (devicePill.getWidthFor (juce::jmin (maxPillWidth, x - 24 - area.getX()))).reduced (0, 1));

    for (int i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->setBounds (x, 0, tabWidths[i], getHeight());
        x += tabWidths[i];
    }
}
