// C:\workspace\Stella AI Studio\src\GalleryBrowser.h
// From KnobMaker (C:\workspace\knobmaker); in Stella its downloads are kept in the studio's own folder.

/*
    GalleryBrowser.h

    The KnobMan community gallery at g200kg.com, mirrored to local disk.

    Three unauthenticated endpoints supply everything:

        gallery.php?m=list                  -> JSON index
        gallery.php?m=get&n={id}&t=bin      -> the raw .knob file
        data/gal/{id}.png                   -> the preview thumbnail

    Browsing does NOT touch the network. One explicit sync copies the whole
    gallery into GalleryLibrary::root(), and from then on the list, the
    thumbnails and importing all read from disk. That is what makes browsing
    instant, and it is also what removes the crash: the old design held a
    thread pool of in-flight HTTP requests wired directly to this component's
    members, and closing the window mid-request tore that out from under them.

    The mirror is a private cache on this machine, for this user. It is not a
    redistributable bundle — licences vary per item and include NC and ND
    variants that cannot ship inside a commercial plugin. The licence is shown
    on every row and the filter defaults to commercial-safe.
*/

#pragma once

#include "KnobFile.h"

#include <functional>

//==============================================================================
struct GalleryItem
{
    int          id = 0;
    juce::String licence;
    juce::String date;
    juce::String author;
    juce::String file;
    juce::String comment;
    juce::String tags;
    int          size = 0;

    /** True for licences that permit commercial use and modification. */
    bool isCommercialSafe() const noexcept;

    /** "$knob", "$slider", "$switch", "$other" — from the tag list. */
    juce::String category() const;
};

//==============================================================================
/** The on-disk mirror: where it lives and what is in it. */
namespace GalleryLibrary
{
    /** Stella: <user application data>/Fanan/Stella AI Studio/gallery */
    juce::File root();

    juce::File indexFile();
    juce::File knobDir();
    juce::File thumbDir();
    juce::File knobFile  (int id);
    juce::File thumbFile (int id);

    /** Turns the gallery's JSON into items. */
    juce::Array<GalleryItem> parseIndex (const juce::String& json);

    /** Reads the saved index. Empty when nothing has been downloaded yet. */
    juce::Array<GalleryItem> loadIndex();

    /** How many of the listed items actually have their .knob file on disk. */
    int countLocal (const juce::Array<GalleryItem>&);
}

//==============================================================================
/** The download itself, deliberately owned by nobody the user can close.

    It used to live inside GalleryBrowser, which meant closing the window ran
    the component's destructor, which called stopThread() on a socket that was
    halfway through a read. When that join timed out JUCE force-killed the
    thread mid-allocation — that was the crash, and it was a coin flip every
    time you closed the window while it was working.

    Now the sync outlives every window. A browser attaches by polling getStatus()
    on its own timer and never touches the thread; closing it does nothing to
    the download. The app shuts the sync down once, in a controlled place. */
class GallerySync final : private juce::Thread
{
public:
    enum class State { Idle, Fetching, Downloading, Paused, Stopped, Finished, Failed };

    struct Status
    {
        State        state = State::Idle;
        juce::String message;
        int          done  = 0;
        int          total = 0;
        bool         indexOnlyRun = false;
    };

    /** Message thread only. */
    static GallerySync& get();

    /** Called from the application's shutdown(), so the thread is joined while
        JUCE is still standing rather than during static teardown. */
    static void shutdownInstance();

    /** One request: refresh the index so we can see what is new. */
    void startIndexOnly();

    /** Index, then every document and preview not already on disk. Pressing
        this after a Stop resumes — anything already downloaded is skipped, so
        it carries on from where it left off. */
    void startFullSync();

    void pause();
    void resume();
    void stop();

    Status getStatus() const;
    bool   isBusy() const;
    bool   isPaused() const noexcept { return pauseRequested.load(); }

    /** Bumped every time the library on disk changes, so a browser can tell it
        needs to re-read without watching the file system. */
    int getRevision() const;

    /** Public only because the single instance is held in a unique_ptr, and the
        default deleter has to be able to see this. The constructor stays
        private, so get() is still the only way to make one. */
    ~GallerySync() override;

private:
    GallerySync();

    void run() override;

    bool fetch (const juce::String& url, juce::MemoryBlock&);
    bool waitWhilePaused();
    void setStatus (State, const juce::String&, int done, int total);
    void bumpRevision();

    mutable juce::CriticalSection lock;
    Status status;
    int    revision = 0;

    std::atomic<bool> pauseRequested { false };
    std::atomic<bool> indexOnly      { false };
    juce::WaitableEvent pauseGate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GallerySync)
};

//==============================================================================
class GalleryBrowser final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::Timer
{
public:
    GalleryBrowser();
    ~GalleryBrowser() override;

    /** Called on the message thread when the user imports a document. */
    std::function<void (const LayerDoc&, const juce::String& name,
                        const juce::StringArray& warnings)> onImport;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr const char* baseUrl = "https://www.g200kg.com/en/webknobman/";

private:
    //==========================================================================
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void timerCallback() override;

    void loadLocalIndex();
    void applyFilter();

    void pollSync();
    void focusNewItems();

    void importSelected();
    void setStatus (const juce::String&);
    void updateButtons();

    bool hasLocalCopy (int id) const;
    int  countMissing() const;

    //==========================================================================
    juce::Array<GalleryItem> allItems;
    juce::Array<GalleryItem> filtered;

    // Message thread only. Nothing else touches these now, so there is no lock
    // and no window in which a worker can write to a destroyed component.
    juce::HashMap<int, juce::Image> thumbs;
    juce::SortedSet<int> thumbsMissing;

    int  lastRevision = -1;
    bool awaitingUpdateFocus = false;

    juce::ListBox    list { "gallery", this };
    juce::TextEditor searchBox;
    juce::ComboBox   licenceBox;
    juce::ComboBox   categoryBox;
    juce::ComboBox   haveBox;
    juce::TextButton updateButton   { "Update" };
    juce::TextButton downloadButton { "Download" };
    juce::TextButton pauseButton    { "Pause" };
    juce::TextButton stopButton     { "Stop" };
    juce::TextButton folderButton   { "Folder" };
    juce::TextButton importButton   { "Import" };
    juce::Label      statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GalleryBrowser)
};
