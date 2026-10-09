// C:\workspace\Stella AI Studio\src\KnobStudio.cpp

#include "KnobStudio.h"
#include "StellaLookAndFeel.h"

namespace
{
    constexpr int listWidth = 240, headerHeight = 54, statusHeight = 26;

    // The New look menu's own entries, after the looks to start from.
    constexpr int emptyId = 10000, galleryId = 10001, knobFileId = 10002, lookFileId = 10003;

    /** A look's starting point when the user asks for an empty one. */
    LayerDoc emptyLook (Looks::Kind kind)
    {
        auto doc = LayerDoc::emptyDocument();

        switch (kind)
        {
            case Looks::Kind::knob:          doc.canvasWidth = doc.canvasHeight = 128; doc.frames = 101; break;
            case Looks::Kind::slider:        doc.canvasWidth = 40;  doc.canvasHeight = 160; doc.frames = 101; break;
            case Looks::Kind::sliderAcross:  doc.canvasWidth = 160; doc.canvasHeight = 40;  doc.frames = 101; break;
            case Looks::Kind::button:        doc.canvasWidth = doc.canvasHeight = 64;  doc.frames = 2; break;
        }

        return doc;
    }

    juce::String withoutExtension (const juce::String& name)
    {
        const auto trimmed = name.trim();
        return trimmed.containsChar ('.') ? trimmed.upToLastOccurrenceOf (".", false, false) : trimmed;
    }
}

//==============================================================================
KnobStudio::KnobStudio (Looks& looksToEdit)
    : looks (looksToEdit)
{
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, Theme::inset);

    newButton.setTooltip ("A new look for this plugin: from a built-in look or one of yours, an empty one, "
                          "one from the KnobMan gallery, or a .knob or .fklayers file");
    newButton.onClick = [this] { showNewMenu(); };

    duplicateButton.setTooltip ("A copy of this look, to change on its own");
    duplicateButton.onClick = [this]
    {
        if (const auto* look = looks.find (current))
        {
            const auto doc = look->doc;
            addLook (look->name, look->kind, doc, look->description);
        }
    };

    mineButton.setTooltip ("Keep this look in My looks, so every project can use it");
    mineButton.onClick = [this]
    {
        if (current.isNotEmpty())
            status (looks.saveToMine (current) ? "\"" + current + "\" is in My looks now: every project can use it."
                                               : "Couldn't save \"" + current + "\" to My looks.");
    };

    deleteButton.setTooltip ("Delete this look from the plugin (only when no control wears it)");
    deleteButton.onClick = [this]
    {
        const auto name = current;

        if (looks.removeProjectLook (name))
            status ("Deleted \"" + name + "\".");
    };

    useButton.onClick = [this]
    {
        if (editingWidget >= 0 && current.isNotEmpty() && onUseLook != nullptr)
        {
            onUseLook (editingWidget, current);
            status ("The " + controlName() + " wears \"" + current + "\" now.");
        }
    };

    uniqueButton.onClick = [this]
    {
        if (editingWidget >= 0 && onGiveOwnLook != nullptr)
        {
            if (const auto name = onGiveOwnLook (editingWidget); name.isNotEmpty())
            {
                select (name);
                status ("The " + controlName() + " has a look of its own now: \"" + name + "\". Changes here change only it.");
            }
        }
    };

    backButton.onClick = [this] { if (onBack != nullptr) onBack(); };

    // The builder: every change goes into the plugin's own looks at once.
    layers.onDocumentChanged = [this]
    {
        const auto* look = looks.find (current);

        if (look == nullptr)
            return;

        {
            const juce::ScopedValueSetter<bool> quiet (saving, true);
            looks.setProjectLook (current, look->kind, layers.getDocument());
        }

        if (const auto* saved = looks.find (current))
            loadedRevision = saved->revision;

        list.repaint();
        updateButtons();
        repaint (headerText);
    };

    layers.onImported = [this] (const LayerDoc& doc, const juce::String& name)
    {
        addLook (withoutExtension (name), kindForNew(), doc);
    };

    layers.onStatus = [this] (const juce::String& text) { status (text == "Ready" ? juce::String() : text); };
    shapes.onStatus = [this] (const juce::String& text) { status (text); };

    tabs.setOutline (0);
    tabs.setTabBarDepth (32);
    tabs.addTab ("Looks", Theme::panel, &layers, false);
    tabs.addTab ("Shapes", Theme::panel, &shapes, false);
    tabs.onChange = [this] (int index)
    {
        if (index == 1)
            shapes.refreshInventory();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &list, &newButton, &duplicateButton, &mineButton, &deleteButton, &backButton })
        addAndMakeVisible (c);

    for (auto* c : std::initializer_list<juce::Component*> { &useButton, &uniqueButton, &tabs })
        addChildComponent (c);

    refreshList();
}

KnobStudio::~KnobStudio()
{
    tabs.clearTabs();
}

//==============================================================================
void KnobStudio::setLayout (const GuiLayout& newLayout)
{
    layout = newLayout;

    if (editedControl() == nullptr)
        editingWidget = -1;

    refreshList();
}

void KnobStudio::editControl (int widgetIndex)
{
    editingWidget = widgetIndex;

    if (const auto* control = editedControl())
    {
        tabs.setCurrentTabIndex (0);
        select (looks.lookFor (*control).name);
    }
    else
    {
        editingWidget = -1;
        updateButtons();
    }

    resized();
    repaint();
}

void KnobStudio::looksChanged()
{
    if (! saving)
        refreshList();
}

void KnobStudio::refreshList()
{
    // What the plugin's controls wear, in the order they come, then its own looks.
    names.clear();

    for (const auto& w : layout.widgets)
        if (Looks::takesLook (w))
            names.addIfNotAlreadyThere (looks.lookFor (w).name);

    for (const auto* look : looks.all())
        if (look->origin == Looks::Origin::project)
            names.addIfNotAlreadyThere (look->name);

    list.updateContent();
    list.repaint();

    select (names.contains (current) ? current : (names.isEmpty() ? juce::String() : names[0]));
}

void KnobStudio::select (const juce::String& name)
{
    const bool sameLook = name == current;
    current = name;
    list.selectRow (names.indexOf (current), false, true);

    // Into the builder: a different look, or this one changed elsewhere (an undo, say).
    if (const auto* look = looks.find (current); look != nullptr && (! sameLook || look->revision != loadedRevision))
    {
        loadedRevision = look->revision;
        layers.setDocument (look->doc);
    }

    tabs.setVisible (looks.find (current) != nullptr);
    updateButtons();
    resized();
    repaint();
}

void KnobStudio::updateButtons()
{
    const auto* look = looks.find (current);
    const auto* control = editedControl();
    const auto wearing = control != nullptr ? looks.lookFor (*control).name : juce::String();

    newButton.setEnabled (looks.isProjectOpen());
    duplicateButton.setEnabled (look != nullptr && looks.isProjectOpen());
    mineButton.setEnabled (look != nullptr);
    deleteButton.setEnabled (look != nullptr && look->origin == Looks::Origin::project && usersOf (current) == 0);

    useButton.setButtonText ("Use on this " + controlName());
    useButton.setTooltip ("The " + controlName() + " you opened wears this look");
    useButton.setVisible (control != nullptr && look != nullptr && wearing != current && look->kind == Looks::kindOf (*control));

    uniqueButton.setButtonText ("Give this " + controlName() + " its own look");
    uniqueButton.setTooltip ("The " + controlName() + " you opened gets a copy of this look, so changes affect only it");
    uniqueButton.setVisible (control != nullptr && wearing == current && usersOf (current) > 1);
}

int KnobStudio::usersOf (const juce::String& name) const
{
    int count = 0;

    for (const auto& w : layout.widgets)
        if (Looks::takesLook (w) && looks.lookFor (w).name == name)
            ++count;

    return count;
}

const GuiWidget* KnobStudio::editedControl() const
{
    if (! juce::isPositiveAndBelow (editingWidget, (int) layout.widgets.size()))
        return nullptr;

    const auto& w = layout.widgets[(size_t) editingWidget];
    return Looks::takesLook (w) ? &w : nullptr;
}

Looks::Kind KnobStudio::kindForNew() const
{
    if (const auto* control = editedControl())
        return Looks::kindOf (*control);

    if (const auto* look = looks.find (current))
        return look->kind;

    return Looks::Kind::knob;
}

juce::String KnobStudio::controlName() const
{
    switch (kindForNew())
    {
        case Looks::Kind::knob:          return "knob";
        case Looks::Kind::slider:
        case Looks::Kind::sliderAcross:  return "slider";
        case Looks::Kind::button:        return "switch";
    }

    return "knob";
}

//==============================================================================
void KnobStudio::addLook (const juce::String& baseName, Looks::Kind kind, const LayerDoc& doc, const juce::String& description)
{
    if (! looks.isProjectOpen())
        return;

    // Never the name of a look a control wears: the new look would take its place on them.
    const auto name = looks.freeName (baseName, names);
    looks.setProjectLook (name, kind, doc, description);

    // Opened from a control: the new look goes onto it.
    bool used = false;

    if (const auto* control = editedControl(); control != nullptr && Looks::kindOf (*control) == kind && onUseLook != nullptr)
    {
        onUseLook (editingWidget, name);
        used = true;
    }

    refreshList();
    select (name);
    tabs.setCurrentTabIndex (0);
    status ("New look \"" + name + "\"" + (used ? juce::String (", on the ") + controlName() : juce::String()) + ".");
}

void KnobStudio::showNewMenu()
{
    if (! looks.isProjectOpen())
    {
        status ("Open a plugin first: its looks are kept in its project.");
        return;
    }

    juce::PopupMenu menu;
    std::vector<std::pair<juce::String, Looks::Origin>> sources;   // by item id - 1

    // The built-in looks and mine, as they come (even one the plugin's own look hides).
    auto addKind = [&] (Looks::Kind kind, const juce::String& title)
    {
        juce::PopupMenu sub;

        for (const auto origin : { Looks::Origin::builtIn, Looks::Origin::mine })
        {
            for (const auto* look : looks.listFrom (origin, kind))
            {
                sources.push_back ({ look->name, origin });
                sub.addItem ((int) sources.size(), look->name + (origin == Looks::Origin::mine ? juce::String ("  (mine)") : juce::String()));
            }
        }

        menu.addSubMenu (title, sub, sub.getNumItems() > 0);
    };

    menu.addSectionHeader ("Start from");
    addKind (Looks::Kind::knob, "Knobs");
    addKind (Looks::Kind::slider, "Sliders");
    addKind (Looks::Kind::sliderAcross, "Sliders across");
    addKind (Looks::Kind::button, "Switches");
    menu.addSeparator();
    menu.addItem (emptyId, "An empty " + Looks::kindDisplayName (kindForNew()));
    menu.addItem (galleryId, juce::String::fromUTF8 ("From the KnobMan gallery\xe2\x80\xa6"));
    menu.addItem (knobFileId, juce::String::fromUTF8 ("Import a KnobMan .knob file\xe2\x80\xa6"));
    menu.addItem (lookFileId, juce::String::fromUTF8 ("Open a .fklayers file\xe2\x80\xa6"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&newButton),
                        [safeThis = juce::Component::SafePointer<KnobStudio> (this), sources] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            auto& self = *safeThis;

                            if (result == emptyId)
                            {
                                const auto kind = self.kindForNew();
                                self.addLook ("New " + Looks::kindDisplayName (kind), kind, emptyLook (kind));
                            }
                            else if (result == galleryId)   self.layers.openGallery();
                            else if (result == knobFileId)  self.layers.importKnobFile();
                            else if (result == lookFileId)  self.openLookFile();
                            else if (juce::isPositiveAndBelow (result - 1, (int) sources.size()))
                            {
                                const auto& [name, origin] = sources[(size_t) (result - 1)];

                                if (const auto* look = self.looks.find (name, origin))
                                {
                                    const auto doc = look->doc;
                                    self.addLook (look->name, look->kind, doc, look->description);
                                }
                            }
                        });
}

void KnobStudio::openLookFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a look (.fklayers)", Looks::myLooksFolder(), juce::String ("*") + Looks::fileExtension);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<KnobStudio> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr || fc.getResult() == juce::File())
                                  return;

                              Looks::Look look;

                              if (Looks::readFile (fc.getResult(), look))
                                  safeThis->addLook (look.name, look.kind, look.doc, look.description);
                              else
                                  safeThis->status ("Couldn't read " + fc.getResult().getFileName() + ": it isn't a KnobMaker layer document.");
                          });
}

void KnobStudio::status (const juce::String& text)
{
    statusText = text;
    repaint (statusArea);
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
    g.drawText (name, 12, 0, width - 96, height, juce::Justification::centredLeft, true);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.5f));
    g.drawText (users == 0 ? juce::String ("unused") : users == 1 ? juce::String ("1 control") : juce::String (users) + " controls",
                width - 92, 0, 82, height, juce::Justification::centredRight, false);
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

    const auto* look = looks.find (current);
    auto header = headerText;

    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (look != nullptr ? "Look: " + current : juce::String ("No looks yet"), header.removeFromTop (20), juce::Justification::centredLeft, true);

    juce::String detail;

    if (look == nullptr)
    {
        detail = looks.isProjectOpen() ? "Build the plugin first: its knobs, sliders and switches bring their looks."
                                       : "Open a plugin: the Knob Studio works on its knobs, sliders and switches.";
    }
    else
    {
        const auto users = usersOf (current);
        detail = (users == 0 ? juce::String ("No control wears it")
                             : "Worn by " + (users == 1 ? juce::String ("1 control") : juce::String (users) + " controls"))
               + juce::String::fromUTF8 (" \xc2\xb7 ")
               + (look->origin == Looks::Origin::project ? juce::String ("this plugin's own look")
                  : look->origin == Looks::Origin::mine  ? juce::String ("from My looks: it becomes this plugin's own when you change it")
                                                         : juce::String ("built in: it becomes this plugin's own when you change it"))
               + ". Every change shows on the plugin at once.";
    }

    g.setColour (Theme::muted);
    g.setFont (Theme::font (13.0f));
    g.drawText (detail, header, juce::Justification::centredLeft, true);

    g.setColour (Theme::outline);
    g.fillRect (0, headerHeight, getWidth(), 1);
    g.fillRect (listWidth, headerHeight, 1, getHeight() - headerHeight - statusHeight);
    g.fillRect (0, statusArea.getY(), getWidth(), 1);

    // The builder's notes (a shadow out of room), and what the buttons did.
    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.5f));
    g.drawText (statusText, statusArea.reduced (16, 0), juce::Justification::centredLeft, true);

    if (look == nullptr)
    {
        auto hint = getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (statusHeight).withTrimmedLeft (listWidth);
        g.setColour (Theme::muted);
        g.setFont (Theme::font (15.0f));
        g.drawFittedText ("Looks are made of layers, the KnobMan way: a shape on each, with its own light, bevel, "
                          "texture and movement. Pick a look on the left, or make one with + New look.",
                          hint.withSizeKeepingCentre (juce::jmin (520, hint.getWidth() - 40), 80), juce::Justification::centred, 3, 1.0f);
    }
}

void KnobStudio::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (16, 8);
    auto buttons = header.withTrimmedTop (4).withTrimmedBottom (4);

    backButton.setBounds (buttons.removeFromRight (150));
    buttons.removeFromRight (10);

    if (uniqueButton.isVisible())
    {
        uniqueButton.setBounds (buttons.removeFromRight (240));
        buttons.removeFromRight (10);
    }

    if (useButton.isVisible())
    {
        useButton.setBounds (buttons.removeFromRight (170));
        buttons.removeFromRight (10);
    }

    headerText = header.withRight (buttons.getRight());

    area.removeFromTop (1);
    statusArea = area.removeFromBottom (statusHeight);

    auto left = area.removeFromLeft (listWidth).reduced (10);
    auto lower = left.removeFromBottom (30);
    left.removeFromBottom (6);
    auto upper = left.removeFromBottom (30);
    left.removeFromBottom (8);
    list.setBounds (left);

    newButton.setBounds (upper.removeFromLeft (upper.getWidth() / 2).reduced (2, 0));
    duplicateButton.setBounds (upper.reduced (2, 0));
    mineButton.setBounds (lower.removeFromLeft (lower.getWidth() * 3 / 5).reduced (2, 0));
    deleteButton.setBounds (lower.reduced (2, 0));

    tabs.setBounds (area.withTrimmedLeft (1));
}
