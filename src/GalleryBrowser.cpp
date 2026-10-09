// C:\workspace\Stella AI Studio\src\GalleryBrowser.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella the gallery's downloads are kept in the
// studio's own folder.

/*
    GalleryBrowser.cpp
*/

#include "GalleryBrowser.h"
#include "UiHelpers.h"

namespace
{
    constexpr int kRowHeight = 56;

    // Short, so a stalled server cannot hold the sync thread past the point
    // where shutdown is willing to wait for it.
    constexpr int kConnectionTimeoutMs = 6000;

    // Read size. Small enough that Stop and app shutdown are answered promptly,
    // large enough not to matter.
    constexpr int kChunkBytes = 16384;
}

//==============================================================================
bool GalleryItem::isCommercialSafe() const noexcept
{
    // Anything carrying NC (non-commercial) or ND (no derivatives) is out.
    if (licence.containsIgnoreCase ("NC") || licence.containsIgnoreCase ("ND"))
        return false;

    return licence.isEmpty()
        || licence.equalsIgnoreCase ("PD")
        || licence.startsWithIgnoreCase ("CC0")
        || licence.startsWithIgnoreCase ("CC-BY");
}

juce::String GalleryItem::category() const
{
    juce::StringArray parts;
    parts.addTokens (tags, ",", {});

    for (auto& p : parts)
    {
        auto t = p.trim();

        if (t.startsWith ("$"))
            return t;
    }

    return "$other";
}

//==============================================================================
namespace GalleryLibrary
{
    juce::File root()
    {
        // Stella: the studio's own copy of the gallery.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Fanan")
                   .getChildFile ("Stella AI Studio")
                   .getChildFile ("gallery");
    }

    juce::File indexFile()      { return root().getChildFile ("index.json"); }
    juce::File knobDir()        { return root().getChildFile ("knob"); }
    juce::File thumbDir()       { return root().getChildFile ("thumb"); }

    juce::File knobFile (int id)  { return knobDir().getChildFile (juce::String (id) + ".knob"); }
    juce::File thumbFile (int id) { return thumbDir().getChildFile (juce::String (id) + ".png"); }

    juce::Array<GalleryItem> parseIndex (const juce::String& json)
    {
        juce::Array<GalleryItem> items;

        auto parsed = juce::JSON::parse (json);

        if (auto* array = parsed.getArray())
        {
            for (const auto& entry : *array)
            {
                GalleryItem item;
                item.id      = entry.getProperty ("id", "0").toString().getIntValue();
                item.licence = entry.getProperty ("license", "").toString();
                item.date    = entry.getProperty ("date", "").toString();
                item.author  = entry.getProperty ("author", "").toString();
                item.file    = entry.getProperty ("file", "").toString();
                item.comment = entry.getProperty ("comment", "").toString();
                item.tags    = entry.getProperty ("tags", "").toString();
                item.size    = entry.getProperty ("size", "0").toString().getIntValue();

                if (item.id > 0)
                    items.add (item);
            }
        }

        return items;
    }

    juce::Array<GalleryItem> loadIndex()
    {
        auto f = indexFile();

        if (! f.existsAsFile())
            return {};

        return parseIndex (f.loadFileAsString());
    }

    int countLocal (const juce::Array<GalleryItem>& items)
    {
        int n = 0;

        for (const auto& item : items)
            if (knobFile (item.id).existsAsFile())
                ++n;

        return n;
    }
}

//==============================================================================
namespace
{
    std::unique_ptr<GallerySync> syncInstance;

    juce::String stoppedMessage (int fetched)
    {
        return "Stopped after " + juce::String (fetched)
                 + " new item" + (fetched == 1 ? "" : "s")
                 + ". Download picks up where it left off.";
    }
}

GallerySync& GallerySync::get()
{
    if (syncInstance == nullptr)
        syncInstance.reset (new GallerySync());

    return *syncInstance;
}

void GallerySync::shutdownInstance()
{
    syncInstance.reset();
}

//==============================================================================
GallerySync::GallerySync() : juce::Thread ("gallery-sync")
{
}

GallerySync::~GallerySync()
{
    signalThreadShouldExit();
    pauseRequested = false;
    pauseGate.signal();

    // Generous, because the alternative is JUCE force-killing a thread that is
    // holding a socket. Reads are chunked, so in practice this returns in well
    // under a second.
    stopThread (15000);
}

//==============================================================================
GallerySync::Status GallerySync::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

int GallerySync::getRevision() const
{
    const juce::ScopedLock sl (lock);
    return revision;
}

bool GallerySync::isBusy() const
{
    return isThreadRunning();
}

void GallerySync::setStatus (State state, const juce::String& message, int done, int total)
{
    const juce::ScopedLock sl (lock);

    status.state        = state;
    status.message      = message;
    status.done         = done;
    status.total        = total;
    status.indexOnlyRun = indexOnly.load();
}

void GallerySync::bumpRevision()
{
    const juce::ScopedLock sl (lock);
    ++revision;
}

//==============================================================================
void GallerySync::startIndexOnly()
{
    if (isThreadRunning())
        return;

    indexOnly      = true;
    pauseRequested = false;
    pauseGate.signal();
    startThread();
}

void GallerySync::startFullSync()
{
    // Already going: treat this as Resume rather than starting a second run.
    if (isThreadRunning())
    {
        resume();
        return;
    }

    indexOnly      = false;
    pauseRequested = false;
    pauseGate.signal();
    startThread();
}

void GallerySync::pause()
{
    pauseRequested = true;
}

void GallerySync::resume()
{
    pauseRequested = false;
    pauseGate.signal();
}

void GallerySync::stop()
{
    // Deliberately does not block. The worker notices and exits on its own; the
    // UI stays live meanwhile, and nothing gets killed.
    signalThreadShouldExit();
    pauseRequested = false;
    pauseGate.signal();
}

//==============================================================================
bool GallerySync::waitWhilePaused()
{
    if (pauseRequested.load())
    {
        {
            const juce::ScopedLock sl (lock);
            status.state   = State::Paused;
            status.message = "Paused at " + juce::String (status.done)
                               + " / " + juce::String (status.total)
                               + " — Resume carries on from here.";
        }

        while (pauseRequested.load() && ! threadShouldExit())
            pauseGate.wait (200);
    }

    return ! threadShouldExit();
}

bool GallerySync::fetch (const juce::String& address, juce::MemoryBlock& destination)
{
    juce::URL url (address);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (kConnectionTimeoutMs);

    auto stream = url.createInputStream (options);

    if (stream == nullptr)
        return false;

    juce::MemoryOutputStream out;
    juce::HeapBlock<char> buffer (kChunkBytes);

    // Chunked rather than writeFromInputStream: that call runs to EOF with no
    // way out, which is what made a stalled transfer un-stoppable.
    while (! stream->isExhausted())
    {
        if (threadShouldExit())
            return false;

        const int got = stream->read (buffer.getData(), kChunkBytes);

        if (got <= 0)
            break;

        out.write (buffer.getData(), (size_t) got);
    }

    if (out.getDataSize() == 0)
        return false;

    destination = out.getMemoryBlock();
    return true;
}

//==============================================================================
void GallerySync::run()
{
    setStatus (State::Fetching, "Fetching the index...", 0, 0);

    juce::MemoryBlock raw;

    if (! fetch (juce::String (GalleryBrowser::baseUrl) + "gallery.php?m=list", raw))
    {
        if (threadShouldExit())
            setStatus (State::Stopped, "Stopped.", 0, 0);
        else
            setStatus (State::Failed, "Could not reach the gallery. Check your connection.", 0, 0);

        return;
    }

    const auto json = juce::String::fromUTF8 (static_cast<const char*> (raw.getData()),
                                              (int) raw.getSize());

    auto items = GalleryLibrary::parseIndex (json);

    if (items.isEmpty())
    {
        setStatus (State::Failed, "The gallery returned something unreadable.", 0, 0);
        return;
    }

    GalleryLibrary::knobDir().createDirectory();
    GalleryLibrary::thumbDir().createDirectory();
    GalleryLibrary::indexFile().replaceWithText (json);
    bumpRevision();

    const int total = items.size();

    if (indexOnly.load())
    {
        int missing = 0;

        for (const auto& item : items)
            if (! GalleryLibrary::knobFile (item.id).existsAsFile())
                ++missing;

        setStatus (State::Finished,
                   missing == 0 ? "Index refreshed — you already have all "
                                    + juce::String (total) + " items."
                                : juce::String (missing) + " new item"
                                    + (missing == 1 ? "" : "s") + " available.",
                   total - missing, total);
        return;
    }

    // Documents first, previews second. Stop halfway and you still have a
    // library you can import from, just with gaps in the artwork.
    int fetched = 0, failed = 0;

    for (int i = 0; i < total; ++i)
    {
        if (! waitWhilePaused())
        {
            setStatus (State::Stopped, stoppedMessage (fetched), i, total);
            return;
        }

        const auto& item = items.getReference (i);

        setStatus (State::Downloading,
                   "Documents  " + juce::String (i + 1) + " / " + juce::String (total)
                     + "   " + item.file,
                   i, total);

        auto destination = GalleryLibrary::knobFile (item.id);

        if (destination.existsAsFile() && destination.getSize() > 0)
            continue;

        juce::MemoryBlock body;

        if (fetch (juce::String (GalleryBrowser::baseUrl) + "gallery.php?m=get&n="
                     + juce::String (item.id) + "&t=bin", body)
            && body.getSize() > 0)
        {
            destination.replaceWithData (body.getData(), body.getSize());
            ++fetched;
            bumpRevision();
        }
        else if (! threadShouldExit())
        {
            ++failed;
        }
    }

    for (int i = 0; i < total; ++i)
    {
        if (! waitWhilePaused())
        {
            setStatus (State::Stopped, stoppedMessage (fetched), i, total);
            return;
        }

        const auto& item = items.getReference (i);

        setStatus (State::Downloading,
                   "Previews  " + juce::String (i + 1) + " / " + juce::String (total),
                   i, total);

        auto destination = GalleryLibrary::thumbFile (item.id);

        if (destination.existsAsFile() && destination.getSize() > 0)
            continue;

        juce::MemoryBlock body;

        if (fetch (juce::String (GalleryBrowser::baseUrl) + "data/gal/"
                     + juce::String (item.id) + ".png", body)
            && body.getSize() > 0)
        {
            destination.replaceWithData (body.getData(), body.getSize());
            bumpRevision();
        }
    }

    setStatus (State::Finished,
               "Library ready — " + juce::String (total) + " items on disk"
                 + (failed > 0 ? ",  " + juce::String (failed) + " unavailable" : "")
                 + ".  Browsing is offline from here.",
               total, total);
}

//==============================================================================
GalleryBrowser::GalleryBrowser()
{
    list.setRowHeight (kRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff202023));
    addAndMakeVisible (list);

    searchBox.setTextToShowWhenEmpty ("Search name, author, tags...", juce::Colours::grey);
    searchBox.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (searchBox);

    licenceBox.addItem ("Commercial-safe only", 1);
    licenceBox.addItem ("All licences", 2);
    licenceBox.setSelectedId (1, juce::dontSendNotification);
    licenceBox.onChange = [this] { applyFilter(); };
    addAndMakeVisible (licenceBox);

    categoryBox.addItem ("All types", 1);
    categoryBox.addItem ("Knobs", 2);
    categoryBox.addItem ("Sliders", 3);
    categoryBox.addItem ("Switches", 4);
    categoryBox.addItem ("Other", 5);
    categoryBox.setSelectedId (1, juce::dontSendNotification);
    categoryBox.onChange = [this] { applyFilter(); };
    addAndMakeVisible (categoryBox);

    haveBox.addItem ("All items", 1);
    haveBox.addItem ("Not downloaded", 2);
    haveBox.addItem ("Downloaded", 3);
    haveBox.setSelectedId (1, juce::dontSendNotification);
    haveBox.onChange = [this] { applyFilter(); };
    addAndMakeVisible (haveBox);

    // Update is one request. It refreshes the index, then the poll switches the
    // list to Not downloaded so the new items are the only thing on screen.
    updateButton.onClick = [this]
    {
        awaitingUpdateFocus = true;
        GallerySync::get().startIndexOnly();
        updateButtons();
    };

    downloadButton.onClick = [this]
    {
        GallerySync::get().startFullSync();
        updateButtons();
    };

    pauseButton.onClick = [this]
    {
        auto& sync = GallerySync::get();

        if (sync.isPaused()) sync.resume();
        else                 sync.pause();

        updateButtons();
    };

    stopButton.onClick = [this] { GallerySync::get().stop(); updateButtons(); };

    folderButton.onClick = [this] { GalleryLibrary::root().revealToUser(); };
    importButton.onClick = [this] { importSelected(); };

    for (auto* b : { &updateButton, &downloadButton, &pauseButton, &stopButton,
                     &folderButton, &importButton })
        addAndMakeVisible (b);

    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (statusLabel);

    // Opens on whatever is already on disk. No network on startup, ever.
    loadLocalIndex();
    lastRevision = GallerySync::get().getRevision();
    updateButtons();

    if (allItems.isEmpty())
        setStatus ("No local library yet. Press Download — it runs in the background, "
                   "and you can close this window while it works.");

    configurePanelControls (*this);

    startTimer (200);
}

GalleryBrowser::~GalleryBrowser()
{
    // Nothing else. The download is not ours to stop, which is the entire point:
    // closing this window used to kill a thread mid-read.
    stopTimer();
}

//==============================================================================
void GalleryBrowser::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void GalleryBrowser::updateButtons()
{
    auto& sync = GallerySync::get();

    const bool busy   = sync.isBusy();
    const bool paused = sync.isPaused();

    updateButton.setEnabled (! busy);

    // Download doubles as Resume: after a Stop, pressing it skips everything
    // already on disk and carries on.
    downloadButton.setButtonText (busy ? "Resume"
                                       : (countMissing() > 0 && ! allItems.isEmpty() ? "Download new"
                                                                                     : "Download"));
    downloadButton.setEnabled (! busy || paused);

    pauseButton.setButtonText (paused ? "Resume" : "Pause");
    pauseButton.setEnabled (busy);

    stopButton.setEnabled (busy);

    folderButton.setEnabled (GalleryLibrary::root().isDirectory());
}

bool GalleryBrowser::hasLocalCopy (int id) const
{
    return GalleryLibrary::knobFile (id).existsAsFile();
}

int GalleryBrowser::countMissing() const
{
    int n = 0;

    for (const auto& item : allItems)
        if (! hasLocalCopy (item.id))
            ++n;

    return n;
}

void GalleryBrowser::focusNewItems()
{
    haveBox.setSelectedId (2, juce::dontSendNotification);
    applyFilter();
}

//==============================================================================
void GalleryBrowser::loadLocalIndex()
{
    allItems = GalleryLibrary::loadIndex();

    thumbs.clear();
    thumbsMissing.clear();

    applyFilter();
}

void GalleryBrowser::pollSync()
{
    auto& sync = GallerySync::get();

    const auto st = sync.getStatus();
    const int revision = sync.getRevision();

    // The library on disk changed under us — new documents, or a fresh index.
    if (revision != lastRevision)
    {
        lastRevision = revision;
        loadLocalIndex();
    }

    if (st.state != GallerySync::State::Idle)
        setStatus (st.message);

    const bool settled = st.state == GallerySync::State::Finished
                      || st.state == GallerySync::State::Stopped
                      || st.state == GallerySync::State::Failed;

    if (awaitingUpdateFocus && settled && ! sync.isBusy())
    {
        awaitingUpdateFocus = false;

        if (st.state == GallerySync::State::Finished && countMissing() > 0)
            focusNewItems();
    }

    updateButtons();
}

//==============================================================================
void GalleryBrowser::applyFilter()
{
    const bool safeOnly = licenceBox.getSelectedId() == 1;
    const auto words = searchBox.getText().toLowerCase();

    const juce::String wanted = [this]() -> juce::String
    {
        switch (categoryBox.getSelectedId())
        {
            case 2:  return "$knob";
            case 3:  return "$slider";
            case 4:  return "$switch";
            case 5:  return "$other";
            default: return {};
        }
    }();

    filtered.clearQuick();

    const int have = haveBox.getSelectedId();

    for (const auto& item : allItems)
    {
        if (safeOnly && ! item.isCommercialSafe())
            continue;

        if (have == 2 &&   hasLocalCopy (item.id)) continue;
        if (have == 3 && ! hasLocalCopy (item.id)) continue;

        if (wanted.isNotEmpty() && item.category() != wanted)
            continue;

        if (words.isNotEmpty())
        {
            const auto haystack = (item.file + " " + item.author + " " + item.comment
                                    + " " + item.tags + " " + item.licence).toLowerCase();

            if (! haystack.contains (words))
                continue;
        }

        filtered.add (item);
    }

    list.updateContent();
    list.repaint();

    if (allItems.isEmpty() || GallerySync::get().isBusy())
        return;

    const int missing = countMissing();

    setStatus (juce::String (filtered.size()) + " of " + juce::String (allItems.size())
                 + " shown  -  " + juce::String (allItems.size() - missing) + " on disk"
                 + (missing > 0 ? ",  " + juce::String (missing) + " not downloaded" : "")
                 + (safeOnly ? "  -  NC and ND licences hidden"
                             : "  -  check licences before shipping"));
}

//==============================================================================
int GalleryBrowser::getNumRows()
{
    return filtered.size();
}

void GalleryBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= filtered.size())
        return;

    const auto& item = filtered.getReference (row);

    if (selected)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (0, 0, w, h);
    }

    auto thumbArea = juce::Rectangle<int> (4, 4, h - 8, h - 8);

    g.setColour (juce::Colour (0xff333338));
    g.fillRect (thumbArea);

    if (thumbs.contains (item.id))
        g.drawImage (thumbs[item.id], thumbArea.toFloat(),
                     juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);

    auto text = juce::Rectangle<int> (thumbArea.getRight() + 8, 2, w - thumbArea.getRight() - 12, h - 4);

    g.setColour (juce::Colour (0xffe0e0e4));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (item.file, text.removeFromTop (18), juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (0xff9a9aa0));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    juce::String line = "#" + juce::String (item.id);

    if (item.author.isNotEmpty())
        line += "  by " + item.author;

    if (! GalleryLibrary::knobFile (item.id).existsAsFile())
        line += "   (not downloaded)";

    g.drawText (line, text.removeFromTop (15), juce::Justification::centredLeft, true);

    const bool safe = item.isCommercialSafe();
    g.setColour (safe ? juce::Colour (0xff7fbf7f) : juce::Colour (0xffd08a60));
    g.drawText (item.licence.isEmpty() ? "PD" : item.licence,
                text.removeFromTop (15), juce::Justification::centredLeft, true);
}

void GalleryBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    importSelected();
}

//==============================================================================
void GalleryBrowser::timerCallback()
{
    pollSync();

    // Thumbnails come off local disk, so they decode on the message thread in
    // well under a millisecond. A few per tick keeps a long list responsive
    // without any of the threading the old remote version needed.
    const int first = list.getRowContainingPosition (2, 2);

    if (first < 0)
        return;

    const int visible = juce::jmax (1, list.getHeight() / kRowHeight + 2);

    int  loaded  = 0;
    bool changed = false;

    for (int i = first; i < juce::jmin (filtered.size(), first + visible); ++i)
    {
        const int id = filtered.getReference (i).id;

        if (thumbs.contains (id) || thumbsMissing.contains (id))
            continue;

        auto file = GalleryLibrary::thumbFile (id);
        auto image = file.existsAsFile() ? juce::ImageFileFormat::loadFrom (file) : juce::Image();

        if (image.isValid())
        {
            thumbs.set (id, image);
            changed = true;
        }
        else
        {
            thumbsMissing.add (id);
        }

        if (++loaded >= 8)
            break;
    }

    if (changed)
        list.repaint();
}

//==============================================================================
void GalleryBrowser::importSelected()
{
    const int row = list.getSelectedRow();

    if (row < 0 || row >= filtered.size())
    {
        setStatus ("Select an item first.");
        return;
    }

    const auto item = filtered.getReference (row);
    auto file = GalleryLibrary::knobFile (item.id);

    if (! file.existsAsFile())
    {
        setStatus (item.file + " is not in the local library. Press Update to fetch it.");
        return;
    }

    // Local file, a few KB: parsed inline. No thread, no download, no wait.
    auto result = KnobFile::parseFile (file);

    if (! result.ok)
    {
        setStatus ("Import failed: " + result.error);
        return;
    }

    if (onImport != nullptr)
        onImport (result.document, item.file, result.warnings);

    setStatus ("Imported " + item.file
                 + " (" + juce::String ((int) result.document.layers.size()) + " layers)");
}

//==============================================================================
void GalleryBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262629));
}

void GalleryBrowser::resized()
{
    auto area = getLocalBounds().reduced (8);

    {
        auto bar = area.removeFromTop (26);
        categoryBox.setBounds (bar.removeFromRight (98));
        bar.removeFromRight (6);
        haveBox.setBounds (bar.removeFromRight (124));
        bar.removeFromRight (6);
        licenceBox.setBounds (bar.removeFromRight (152));
        bar.removeFromRight (6);
        searchBox.setBounds (bar);
    }

    area.removeFromTop (6);

    {
        auto bar = area.removeFromTop (26);

        auto place = [&bar] (juce::Component& c, int w)
        {
            c.setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (6);
        };

        place (updateButton,   76);
        place (downloadButton, 116);
        place (pauseButton,    76);
        place (stopButton,     64);
        place (folderButton,   72);
    }

    area.removeFromTop (6);

    {
        auto bottom = area.removeFromBottom (28);
        importButton.setBounds (bottom.removeFromRight (100));
        bottom.removeFromRight (8);
        statusLabel.setBounds (bottom);
    }

    area.removeFromBottom (6);
    list.setBounds (area);
}
