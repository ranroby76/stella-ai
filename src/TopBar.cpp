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

    constexpr int modeGroup = 4101;
}

//==============================================================================
TopBar::TopBar()
{
    projectButton.setTooltip ("New, open, the cloud and recent projects");
    projectButton.onClick = [this] { showProjectMenu(); };


    undoButton.onClick = [this] { if (onUndo != nullptr) onUndo(); };
    redoButton.onClick = [this] { if (onRedo != nullptr) onRedo(); };
    setUndoState ({}, {});

    for (auto* button : { &designButton, &playButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (modeGroup);
        button->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    }

    designButton.setTooltip ("Design mode: shape the plugin's GUI and blocks");
    playButton.setTooltip ("Play mode: use the plugin exactly as it will be in a DAW");

    designButton.setToggleState (true, juce::dontSendNotification);
    designButton.onClick = [this] { if (designButton.getToggleState() && onModeChanged != nullptr) onModeChanged (false); };
    playButton.onClick   = [this] { if (playButton.getToggleState() && onModeChanged != nullptr) onModeChanged (true); };

    for (auto* button : { &projectButton, &undoButton, &redoButton, &designButton, &playButton })
        addAndMakeVisible (button);

    fananLogo = juce::ImageCache::getFromMemory (StellaAssets::fanan_logo_png, StellaAssets::fanan_logo_pngSize);

    setProjectName ({});
}

//==============================================================================
void TopBar::setProjectName (const juce::String& name)
{
    hasProject = name.isNotEmpty();
    projectButton.setButtonText ((hasProject ? name : juce::String ("No project")) + juce::String::fromUTF8 ("  \xe2\x96\xbe"));
}

void TopBar::setPlayMode (bool playMode)
{
    (playMode ? playButton : designButton).setToggleState (true, juce::dontSendNotification);
}

//==============================================================================
void TopBar::showProjectMenu()
{
    juce::PopupMenu menu;
    const auto recent = recentProjects != nullptr ? recentProjects() : juce::StringArray();
    const bool cloud = cloudAvailable != nullptr && cloudAvailable();

    menu.addItem (newId, juce::String::fromUTF8 ("New plugin\xe2\x80\xa6"));
    menu.addItem (openId, juce::String::fromUTF8 ("Open\xe2\x80\xa6"));
    menu.addSeparator();

    if (cloudStatus != nullptr)
    {
        menu.addItem (recentBaseId - 2, cloudStatus(), false);
        menu.addItem (cloudSaveId, "Save to the cloud now", cloud && hasProject);
        menu.addItem (cloudOpenId, juce::String::fromUTF8 ("Open from the cloud\xe2\x80\xa6"), cloud);
        menu.addSeparator();
    }

    if (recent.isEmpty())
    {
        menu.addItem (recentBaseId - 1, "No recent projects", false);
    }
    else
    {
        menu.addSectionHeader ("Recent");

        for (int i = 0; i < recent.size(); ++i)
        {
            const juce::File file (recent[i]);
            menu.addItem (recentBaseId + i, file.getParentDirectory().getFileName());
        }
    }

    menu.addSeparator();
    menu.addItem (revealId, "Show project folder", hasProject);
    menu.addItem (closeId, "Close project", hasProject);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&projectButton),
                        [safeThis = juce::Component::SafePointer<TopBar> (this), recent] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

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

    projectButton.setBounds (area.removeFromLeft (260));
    area.removeFromLeft (12);
    undoButton.setBounds (area.removeFromLeft (58));
    area.removeFromLeft (4);
    redoButton.setBounds (area.removeFromLeft (58));
    area.removeFromLeft (12);

    auto modes = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 152), area.getHeight());
    designButton.setBounds (modes.removeFromLeft (modes.getWidth() / 2));
    playButton.setBounds (modes);

    designButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    playButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
}
