// C:\workspace\Stella AI Studio\src\TopBar.cpp

#include "TopBar.h"
#include "StellaLookAndFeel.h"

#include "StellaAssets.h"

namespace
{
    enum MenuIds
    {
        revealId = 1,
        closeId  = 2,
        cloudSaveId = 3,
        cloudOpenId = 4,
        newId = 5,
        openId = 6,
        recentBaseId = 100
    };

    const juce::Font tabFont()      { return Theme::font (14.5f, true); }
    const juce::Font menuFont()     { return Theme::font (14.5f); }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return (int) std::ceil (glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true).getWidth());
    }
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

//==============================================================================
TopBar::TopBar()
{
    fileMenu.setTooltip ("New, open, the cloud and recent projects");
    fileMenu.onClick = [this] { showFileMenu(); };

    undoButton.onClick = [this] { if (onUndo != nullptr) onUndo(); };
    redoButton.onClick = [this] { if (onRedo != nullptr) onRedo(); };
    setUndoState ({}, {});

    for (auto* button : std::initializer_list<juce::Component*> { &fileMenu, &undoButton, &redoButton })
        addAndMakeVisible (button);

    for (int i = 0; i < numTabs; ++i)
    {
        auto* tab = tabs.add (new TabButton (tabNames[i]));
        tab->onClick = [this, i] { if (onTabChosen != nullptr) onTabChosen (i); };
        addAndMakeVisible (tab);
    }

    setCurrentTab (0);

    fananLogo = juce::ImageCache::getFromMemory (StellaAssets::fanan_logo_png, StellaAssets::fanan_logo_pngSize);

    setProjectName ({});
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

//==============================================================================
void TopBar::showFileMenu()
{
    // A plain Windows-style menu: text items, separators, and a submenu for recent projects.
    const auto recent = recentProjects != nullptr ? recentProjects() : juce::StringArray();
    const bool cloud = cloudAvailable != nullptr && cloudAvailable();

    juce::PopupMenu menu;
    menu.addItem (newId, juce::String::fromUTF8 ("New plugin\xe2\x80\xa6"));
    menu.addItem (openId, juce::String::fromUTF8 ("Open\xe2\x80\xa6"));

    juce::PopupMenu recentMenu;

    for (int i = 0; i < recent.size(); ++i)
        recentMenu.addItem (recentBaseId + i, juce::File (recent[i]).getParentDirectory().getFileName());

    menu.addSubMenu ("Open recent", recentMenu, ! recent.isEmpty());
    menu.addSeparator();

    // The cloud's state sits on the right of its item, where Windows shows shortcuts.
    juce::PopupMenu::Item save ("Save to the cloud now");
    save.itemID = cloudSaveId;
    save.isEnabled = cloud && hasProject;

    if (save.isEnabled && cloudStatus != nullptr)
        save.shortcutKeyDescription = cloudStatus().fromFirstOccurrenceOf ("Cloud: ", false, false);

    menu.addItem (save);
    menu.addItem (cloudOpenId, juce::String::fromUTF8 ("Open from the cloud\xe2\x80\xa6"), cloud);
    menu.addSeparator();
    menu.addItem (revealId, "Show project folder", hasProject);
    menu.addItem (closeId, "Close project", hasProject);

    fileMenu.setOpen (true);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileMenu).withMinimumWidth (220),
                        [safeThis = juce::Component::SafePointer<TopBar> (this), recent] (int result)
                        {
                            if (safeThis == nullptr)
                                return;

                            safeThis->fileMenu.setOpen (false);

                            if (result == newId && safeThis->onNew != nullptr)
                                safeThis->onNew();
                            else if (result == openId && safeThis->onOpen != nullptr)
                                safeThis->onOpen();
                            else if (result == cloudSaveId && safeThis->onSaveToCloud != nullptr)
                                safeThis->onSaveToCloud();
                            else if (result == cloudOpenId && safeThis->onOpenFromCloud != nullptr)
                                safeThis->onOpenFromCloud();
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
    juce::GlyphArrangement first;
    const auto bold = Theme::font (17.0f, true);
    const auto light = Theme::font (17.0f);

    first.addLineOfText (bold, "STELLA", 0.0f, 0.0f);
    const auto firstWidth = first.getBoundingBox (0, first.getNumGlyphs(), true).getWidth();

    auto area = logoArea.toFloat();
    g.setFont (bold);
    g.setColour (Theme::accent);
    g.drawText ("STELLA", area, juce::Justification::centredLeft, false);

    g.setFont (light);
    g.setColour (Theme::muted);
    g.drawText ("AI STUDIO", area.withTrimmedLeft (firstWidth + 6.0f), juce::Justification::centredLeft, false);

    // The Fanan logo, right-aligned, as tall as the bar allows.
    if (fananLogo.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageWithin (fananLogo, fananArea.getX(), fananArea.getY(), fananArea.getWidth(), fananArea.getHeight(),
                           juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid | juce::RectanglePlacement::onlyReduceInSize);
    }
}

void TopBar::setUndoState (const juce::String& undoLabel, const juce::String& redoLabel)
{
    undoButton.setEnabled (undoLabel.isNotEmpty());
    redoButton.setEnabled (redoLabel.isNotEmpty());
    undoButton.setTooltip (undoLabel.isNotEmpty() ? "Undo: " + undoLabel + " (Ctrl+Z)" : juce::String ("Nothing to undo"));
    redoButton.setTooltip (redoLabel.isNotEmpty() ? "Redo: " + redoLabel + " (Ctrl+Y)" : juce::String ("Nothing to redo"));
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    area.removeFromBottom (1);

    logoArea = area.removeFromLeft (172);

    // The logo keeps its own shape (433 x 171), fitted to the bar's height.
    const auto logoHeight = area.getHeight();
    const auto logoWidth = fananLogo.isValid() ? juce::roundToInt ((float) logoHeight * (float) fananLogo.getWidth() / (float) fananLogo.getHeight()) : 0;
    fananArea = area.removeFromRight (logoWidth);
    area.removeFromRight (12);

    fileMenu.setBounds (area.removeFromLeft (textWidth (menuFont(), fileMenu.getButtonText()) + 22));
    area.removeFromLeft (12);
    undoButton.setBounds (area.removeFromLeft (58));
    area.removeFromLeft (4);
    redoButton.setBounds (area.removeFromLeft (58));

    // The tabs: centred in the window and as tall as the bar, never over the left group.
    int total = 0;

    for (auto* tab : tabs)
        total += textWidth (tabFont(), tab->getButtonText()) + 40;

    auto x = juce::jmax (redoButton.getRight() + 24, (getWidth() - total) / 2);

    for (auto* tab : tabs)
    {
        const auto width = textWidth (tabFont(), tab->getButtonText()) + 40;
        tab->setBounds (x, 0, width, getHeight());
        x += width;
    }
}
