// C:\workspace\Stella AI Studio\src\Main.cpp

#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"
#include "Settings.h"
#include "StellaLookAndFeel.h"

//==============================================================================
class StellaApplication final : public juce::JUCEApplication
{
public:
    StellaApplication() = default;

    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }

    // One instance only: most ASIO drivers can be opened by a single process at a time.
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        settings = std::make_unique<Settings>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr && settings != nullptr)
            settings->setWindowState (mainWindow->getWindowStateAsString());

        mainWindow = nullptr;
        settings = nullptr;

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        // Projects save as they go, so there is nothing to ask before quitting.
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

    //==============================================================================
    /**
        A standard window: the operating system's own title bar with minimise, maximise
        and close, resizable from every edge.

        It opens maximised (full screen, title bar and taskbar still showing). The
        title bar's restore button takes it back to its normal size: the one it had
        last time while that still fits a screen, otherwise one fitted to the main
        screen. A window bigger than the screen (a small monitor, or display scaling
        such as 125% or 150%) would hide its own title bar and edges.
    */
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, Settings& settings)
            : DocumentWindow (name, Theme::window, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (settings), true);
            setResizable (true, false);

            // The smallest size the layout works at, unless the screen itself is smaller.
            const auto room = getFrame().subtractedFrom (getWorkArea (true));
            setResizeLimits (juce::jmin (minimumWidth, room.getWidth()),
                             juce::jmin (minimumHeight, room.getHeight()),
                             32768, 32768);

            // First the normal size, for the restore button...
            if (restoreWindowStateFromString (withoutFullScreen (settings.getWindowState())))
                keepOnScreen();
            else
                placeForFirstLaunch();

            // ...then open maximised. JUCE's own restore path keeps the normal size for the
            // restore button.
           #if JUCE_WINDOWS
            restoreWindowStateFromString ("fs " + getBounds().toString());   // appears already maximised
            setVisible (true);
           #else
            setVisible (true);   // macOS and Linux only maximise a window that is already showing
            restoreWindowStateFromString ("fs " + getBounds().toString());
           #endif
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        static constexpr int preferredWidth  = 1440;
        static constexpr int preferredHeight = 900;
        static constexpr int minimumWidth    = 880;
        static constexpr int minimumHeight   = 600;

        /** A saved state without its "maximised" mark, so only its normal size is restored. */
        static juce::String withoutFullScreen (const juce::String& state)
        {
            const auto trimmed = state.trim();
            return trimmed.startsWithIgnoreCase ("fs") ? trimmed.substring (2).trim() : trimmed;
        }

        /** The native frame around the content: the title bar on top, thin borders elsewhere. */
        juce::BorderSize<int> getFrame() const
        {
            if (auto* peer = getPeer())
                if (const auto frame = peer->getFrameSizeIfPresent())
                    return *frame;

            return { 32, 8, 8, 8 };   // a typical frame, until the system reports the real one
        }

        /** The usable part of a screen (without the taskbar), in the same units as the window. */
        juce::Rectangle<int> getWorkArea (bool primary) const
        {
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto* display = primary ? displays.getPrimaryDisplay()
                                          : displays.getDisplayForRect (getFrame().addedTo (getScreenBounds()));

            if (display == nullptr)
                display = displays.getPrimaryDisplay();

            return display != nullptr ? display->userBounds.getLargestIntegerWithin()
                                      : juce::Rectangle<int> (0, 0, 1280, 720);
        }

        /** First launch: large, but with the title bar and every edge on the main screen. */
        void placeForFirstLaunch()
        {
            const auto room = getFrame().subtractedFrom (getWorkArea (true));

            const auto width  = juce::jmin (preferredWidth,  juce::roundToInt ((float) room.getWidth()  * 0.94f));
            const auto height = juce::jmin (preferredHeight, juce::roundToInt ((float) room.getHeight() * 0.94f));

            setBounds (room.withSizeKeepingCentre (width, height));
        }

        /** A remembered position is kept unless it no longer fits: then it shrinks and moves in. */
        void keepOnScreen()
        {
            if (isFullScreen() || isMinimised())
                return;   // maximised: the system places it

            const auto frame = getFrame();
            const auto work = getWorkArea (false);
            auto outer = frame.addedTo (getScreenBounds());   // the window as the user sees it

            if (work.contains (outer))
                return;

            outer.setSize (juce::jmin (outer.getWidth(), work.getWidth()),
                           juce::jmin (outer.getHeight(), work.getHeight()));

            outer.setPosition (juce::jlimit (work.getX(), work.getRight() - outer.getWidth(), outer.getX()),
                               juce::jlimit (work.getY(), work.getBottom() - outer.getHeight(), outer.getY()));

            setBounds (frame.subtractedFrom (outer));
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    StellaLookAndFeel lookAndFeel;   // outlives every window
    std::unique_ptr<Settings> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (StellaApplication)
