// C:\workspace\Stella AI Studio\src\Workspace.cpp

#include "Workspace.h"
#include "StellaLookAndFeel.h"

//==============================================================================
Workspace::PluginView::PluginView()
{
    newButton.setButtonText (juce::String::fromUTF8 ("New plugin\xe2\x80\xa6"));
    openButton.setButtonText (juce::String::fromUTF8 ("Open\xe2\x80\xa6"));

    newButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    newButton.onClick = [this] { if (onNew != nullptr) onNew(); };
    openButton.onClick = [this] { if (onOpen != nullptr) onOpen(); };

    buildButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    buildButton.setTooltip ("Compile the plugin and play it live (it keeps playing while it rebuilds)");
    buildButton.onClick = [this] { if (onBuild != nullptr) onBuild(); };

    logButton.setTooltip ("The compiler's messages from the last build");
    logButton.onClick = [this] { if (onShowLog != nullptr) onShowLog(); };

    exportButton.setTooltip ("Build the plugin as files your DAW loads (CLAP and VST3, Windows 64-bit)");
    exportButton.onClick = [this] { if (onExport != nullptr) onExport(); };

    addButton.setTooltip ("Add a knob, slider, switch, selector, label or group");
    addButton.onClick = [this] { showAddMenu(); };

    autoButton.setTooltip ("Replace the GUI with a plain automatic one: a group per module, a control per parameter");
    autoButton.onClick = [this] { if (onAutoLayout != nullptr) onAutoLayout(); };

    addAndMakeVisible (newButton);
    addAndMakeVisible (openButton);
    addChildComponent (buildButton);
    addChildComponent (logButton);
    addChildComponent (exportButton);

    presetBox.setTextWhenNothingSelected ("No preset");
    presetBox.setTextWhenNoChoicesAvailable ("No presets yet");
    presetBox.setTooltip ("Choose a preset: all its values at once");
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0 && onPresetChosen != nullptr)
            onPresetChosen (presetBox.getSelectedId() - 1);
    };

    savePresetButton.setButtonText (juce::String::fromUTF8 ("Save preset\xe2\x80\xa6"));
    savePresetButton.setTooltip ("Save the current values as a preset (it goes into the exported plugin too)");
    savePresetButton.onClick = [this] { if (onSavePreset != nullptr) onSavePreset(); };

    deletePresetButton.setTooltip ("Delete the chosen preset");
    deletePresetButton.onClick = [this]
    {
        if (presetBox.getSelectedId() > 0 && onDeletePreset != nullptr)
            onDeletePreset (presetBox.getSelectedId() - 1);
    };

    for (auto* button : { &aButton, &bButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (4242);
        button->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    }

    aButton.setTooltip ("Compare two settings: A and B each keep their own values");
    bButton.setTooltip ("Compare two settings: A and B each keep their own values");
    aButton.setToggleState (true, juce::dontSendNotification);
    aButton.onClick = [this] { if (aButton.getToggleState() && abSlot != 0 && onAbChosen != nullptr) onAbChosen (0); };
    bButton.onClick = [this] { if (bButton.getToggleState() && abSlot != 1 && onAbChosen != nullptr) onAbChosen (1); };
    aButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    bButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    copyButton.setTooltip ("Copy the values of the side that's on to the other side");
    copyButton.onClick = [this] { if (onAbCopy != nullptr) onAbCopy(); };

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &savePresetButton, &deletePresetButton, &aButton, &bButton, &copyButton })
        addChildComponent (c);
    addChildComponent (addButton);
    addChildComponent (autoButton);
    addChildComponent (canvas);
}

void Workspace::PluginView::showAddMenu()
{
    juce::PopupMenu menu;
    const char* names[] { "Knob", "Slider", "Switch", "Selector", "XY pad",
                          "Meter", "Lamp", "Scope", "Envelope curve", "Filter curve",
                          "Presets", "Label", "Group", "Shape" };
    const GuiWidget::Type types[] { GuiWidget::Type::knob, GuiWidget::Type::slider, GuiWidget::Type::toggle, GuiWidget::Type::selector, GuiWidget::Type::xy,
                                    GuiWidget::Type::meter, GuiWidget::Type::lamp, GuiWidget::Type::scope, GuiWidget::Type::envelope, GuiWidget::Type::filter,
                                    GuiWidget::Type::preset, GuiWidget::Type::label, GuiWidget::Type::group, GuiWidget::Type::shape };

    for (int i = 0; i < 14; ++i)
    {
        if (i == 5 || i == 10 || i == 11)
            menu.addSeparator();

        menu.addItem (i + 1, names[i]);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [safeThis = juce::Component::SafePointer<PluginView> (this), types] (int result)
                        {
                            if (safeThis != nullptr && result > 0)
                                safeThis->canvas.addWidget (types[result - 1]);
                        });
}

void Workspace::PluginView::setProject (const juce::String& name, const juce::String& kindName)
{
    projectName = name;
    kind = kindName;

    const bool hasProject = projectName.isNotEmpty();

    newButton.setVisible (! hasProject);
    openButton.setVisible (! hasProject);
    buildButton.setVisible (hasProject);
    logButton.setVisible (hasProject);
    exportButton.setVisible (hasProject);

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &savePresetButton, &deletePresetButton, &aButton, &bButton, &copyButton })
        c->setVisible (hasProject);
    canvas.setVisible (hasProject);
    setPlayMode (playMode);

    resized();
    repaint();
}

void Workspace::PluginView::setPlayMode (bool shouldPlay)
{
    playMode = shouldPlay;

    const bool designTools = ! playMode && projectName.isNotEmpty();
    addButton.setVisible (designTools);
    autoButton.setVisible (designTools);

    canvas.setDesignMode (! playMode);
    repaint();
}

void Workspace::PluginView::setBuild (BuildState state, const juce::String& status)
{
    buildState = state;
    buildStatus = status;
    buildButton.setEnabled (state != BuildState::building);
    repaint (getLocalBounds().removeFromTop (barHeight));
}

void Workspace::PluginView::setPresets (const juce::StringArray& names, int selected)
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addItemList (names, 1);
    presetBox.setSelectedId (selected >= 0 ? selected + 1 : 0, juce::dontSendNotification);
    deletePresetButton.setEnabled (selected >= 0);
    canvas.setPresets (names, selected);
}

void Workspace::PluginView::setAbSlot (int slot)
{
    abSlot = slot;
    (slot == 0 ? aButton : bButton).setToggleState (true, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "Copy A to B" : "Copy B to A");
}

void Workspace::PluginView::setExporting (bool exporting)
{
    exportButton.setEnabled (! exporting);
    exportButton.setButtonText (exporting ? juce::String::fromUTF8 ("Exporting\xe2\x80\xa6") : juce::String ("Export"));
}

void Workspace::PluginView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    if (projectName.isEmpty())
    {
        auto area = getLocalBounds().withSizeKeepingCentre (440, 150);

        g.setColour (Theme::text);
        g.setFont (Theme::font (24.0f, true));
        g.drawText ("Build a plugin by describing it", area.removeFromTop (34), juce::Justification::centred, false);

        g.setColour (Theme::muted);
        g.setFont (Theme::font (15.0f));
        g.drawText ("Start a new one, or open a plugin you're working on.", area.removeFromTop (26),
                    juce::Justification::centred, false);
        return;
    }

    // The build bar: a light for the state, and what's happening.
    auto bar = getLocalBounds().removeFromTop (barHeight).reduced (16, 10);
    bar.removeFromLeft (buildButton.getWidth() + 14);
    bar.removeFromRight (logButton.getWidth() + exportButton.getWidth() + 20);

    if (addButton.isVisible())
        bar.removeFromRight (addButton.getWidth() + autoButton.getWidth() + 20);

    const auto lightColour = buildState == BuildState::playing  ? Theme::safe
                           : buildState == BuildState::building ? Theme::hot
                           : buildState == BuildState::failed   ? Theme::clip
                                                                : Theme::muted;

    const auto light = bar.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (lightColour.withAlpha (0.35f));
    g.fillEllipse (light.expanded (2.5f));
    g.setColour (lightColour);
    g.fillEllipse (light);

    bar.removeFromLeft (8);
    g.setColour (buildState == BuildState::failed ? Theme::clip.brighter (0.4f) : Theme::text.withAlpha (0.85f));
    g.setFont (Theme::font (14.0f));
    g.drawText (buildStatus.isNotEmpty() ? buildStatus : juce::String ("Not built yet"), bar, juce::Justification::centredLeft, true);

    g.setColour (Theme::outline);
    g.fillRect (0, barHeight - 1, getWidth(), 1);
    g.fillRect (0, barHeight + presetRowHeight - 1, getWidth(), 1);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (13.5f));
    g.drawText ("Preset", juce::Rectangle<int> (16, barHeight, 60, presetRowHeight), juce::Justification::centredLeft, false);
}

void Workspace::PluginView::resized()
{
    auto buttons = getLocalBounds().withSizeKeepingCentre (300, 40).translated (0, 64);
    newButton.setBounds (buttons.removeFromLeft (150).reduced (4, 0));
    openButton.setBounds (buttons.reduced (4, 0));

    auto bar = getLocalBounds().removeFromTop (barHeight).reduced (16, 9);
    buildButton.setBounds (bar.removeFromLeft (124));
    logButton.setBounds (bar.removeFromRight (64));
    bar.removeFromRight (6);
    exportButton.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (14);
    autoButton.setBounds (bar.removeFromRight (104));
    bar.removeFromRight (6);
    addButton.setBounds (bar.removeFromRight (72));

    auto row = getLocalBounds().withTrimmedTop (barHeight).removeFromTop (presetRowHeight).reduced (16, 6);
    row.removeFromLeft (56);
    presetBox.setBounds (row.removeFromLeft (220));
    row.removeFromLeft (8);
    savePresetButton.setBounds (row.removeFromLeft (112));
    row.removeFromLeft (6);
    deletePresetButton.setBounds (row.removeFromLeft (70));

    copyButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    bButton.setBounds (row.removeFromRight (36));
    aButton.setBounds (row.removeFromRight (36));

    canvas.setBounds (getLocalBounds().withTrimmedTop (barHeight + presetRowHeight));
}

//==============================================================================
Workspace::HintView::HintView (juce::String titleWithProject, juce::String detailWithProject, bool dotGrid)
    : title (std::move (titleWithProject)), detail (std::move (detailWithProject)), grid (dotGrid)
{
}

void Workspace::HintView::setHasProject (bool hasProject)
{
    project = hasProject;
    repaint();
}

void Workspace::HintView::paint (juce::Graphics& g)
{
    g.fillAll (grid ? Theme::inset : Theme::panel);

    auto area = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 90);

    g.setColour (Theme::text);
    g.setFont (Theme::font (20.0f, true));
    g.drawText (project ? title : juce::String ("No project open"), area.removeFromTop (30),
                juce::Justification::centred, false);

    // A scale of 1 wraps long text onto more lines instead of squashing it onto one.
    g.setColour (Theme::muted);
    g.setFont (Theme::font (14.5f));
    g.drawFittedText (project ? detail : juce::String ("Start a new plugin or open one from the top bar."),
                      area, juce::Justification::centredTop, 3, 1.0f);
}

//==============================================================================
Workspace::Workspace()
{
    pluginView.onNew        = [this] { if (onNewRequested != nullptr) onNewRequested(); };
    pluginView.onOpen       = [this] { if (onOpenRequested != nullptr) onOpenRequested(); };
    pluginView.onBuild      = [this] { if (onBuildRequested != nullptr) onBuildRequested(); };
    pluginView.onShowLog    = [this] { if (onShowLogRequested != nullptr) onShowLogRequested(); };
    pluginView.onExport     = [this] { if (onExportRequested != nullptr) onExportRequested(); };
    pluginView.onSavePreset = [this] { if (onSavePreset != nullptr) onSavePreset(); };
    pluginView.onAbCopy     = [this] { if (onAbCopy != nullptr) onAbCopy(); };
    pluginView.onPresetChosen = [this] (int i) { if (onPresetChosen != nullptr) onPresetChosen (i); };
    pluginView.onDeletePreset = [this] (int i) { if (onDeletePreset != nullptr) onDeletePreset (i); };
    pluginView.onAbChosen     = [this] (int slot) { if (onAbChosen != nullptr) onAbChosen (slot); };
    pluginView.canvas.onPresetChosen = [this] (int i) { if (onPresetChosen != nullptr) onPresetChosen (i); };
    pluginView.onAutoLayout = [this] { if (onAutoLayoutRequested != nullptr) onAutoLayoutRequested(); };

    pluginView.canvas.onLayoutEdited = [this] { if (onLayoutEdited != nullptr) onLayoutEdited(); };

    // Double-click a knob in Design mode: its look opens in the Knob Studio.
    pluginView.canvas.onEditKnob = [this] (int widgetIndex)
    {
        studio.setLayout (pluginView.canvas.getLayout());
        studio.editKnob (widgetIndex);
        tabs.setCurrentTabIndex (studioTab);
    };

    studio.onStyleChanged = [this] (const juce::String& name, const KnobStyle& style) { pluginView.canvas.setKnobStyle (name, style); };

    pluginView.canvas.readLevel = [this] (const juce::String& source, bool rms) { return readLevel != nullptr ? readLevel (source, rms) : 0.0f; };
    pluginView.canvas.readScope = [this] (const juce::String& source, float* destination, int numSamples)
    {
        if (readScope != nullptr)
            readScope (source, destination, numSamples);
        else
            std::fill (destination, destination + numSamples, 0.0f);
    };
    pluginView.canvas.onSourcesChanged = [this] (const juce::StringArray& levels, const juce::StringArray& scopes)
    {
        if (onSourcesChanged != nullptr)
            onSourcesChanged (levels, scopes);
    };
    studio.onMakeUnique = [this] (int widgetIndex)
    {
        const auto name = pluginView.canvas.makeKnobUnique (widgetIndex);
        studio.setLayout (pluginView.canvas.getLayout());
        return name;
    };
    studio.onBack = [this] { tabs.setCurrentTabIndex (pluginTab); };
    pluginView.canvas.onParameterChanged = [this] (int index, float value)
    {
        if (onParameterChanged != nullptr)
            onParameterChanged (index, value);
    };

    tabs.setTabBarDepth (36);
    tabs.setOutline (0);
    tabs.setIndent (0);

    tabs.addTab ("Plugin", Theme::panel, &pluginView, false);
    tabs.addTab ("Schematic", Theme::inset, &schematicView, false);
    tabs.addTab ("Knob Studio", Theme::panel, &studio, false);

    addAndMakeVisible (tabs);
}

Workspace::~Workspace()
{
    tabs.clearTabs();
}

void Workspace::setProject (const juce::String& name, const juce::String& kindName)
{
    pluginView.setProject (name, kindName);
}

void Workspace::setPlayMode (bool playMode)                               { pluginView.setPlayMode (playMode); }
void Workspace::setBuild (BuildState state, const juce::String& status)   { pluginView.setBuild (state, status); }
void Workspace::setExporting (bool exporting)                             { pluginView.setExporting (exporting); }
void Workspace::setParameters (const juce::Array<ParamControl>& params)   { pluginView.canvas.setParameters (params); }
void Workspace::setLayout (const GuiLayout& layout)
{
    pluginView.canvas.setLayout (layout);
    studio.setLayout (layout);
}
void Workspace::showTab (TabIndex index)                                  { tabs.setCurrentTabIndex ((int) index); }

void Workspace::setPresets (const juce::StringArray& names, int selected)    { pluginView.setPresets (names, selected); }
void Workspace::setAbSlot (int slot)                                         { pluginView.setAbSlot (slot); }

void Workspace::setSources (const juce::StringArray& signals, const juce::StringArray& displays)
{
    pluginView.canvas.setSources (signals, displays);
}

void Workspace::resized()
{
    tabs.setBounds (getLocalBounds());
}
