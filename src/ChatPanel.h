// C:\workspace\Stella AI Studio\src\ChatPanel.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FananServer.h"
#include "StellaAi.h"

#include <functional>
#include <vector>

//==============================================================================
/**
    The Stella AI panel.

    Header: the title on the left and the connection light with the credits in the top
    right corner; under them the account box, and two equal yellow pill buttons: Sign in /
    Sign out, and Buy credits (only once signed in).

    Signed out, the account box takes an email (passed to the site's sign-in). When the
    mouse moves into it, it suggests the email last signed in with; a click fills it in.
    Signed in, it shows the account's email.

    Below: until the user signs in, only "Sign in to start using Stella AI". Once signed
    in, the conversation, the guide and the request box appear.

    When the studio can't get online, a warning sits above the body (with Retry) until it
    can: no internet, or Fanan's server not answering, each with its own words.
*/
class ChatPanel final : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    struct Account
    {
        FananServer::Status status = FananServer::Status::checking;
        bool siteAvailable = false;
        bool signedIn = false;
        bool waitingForBrowser = false;   // sent to the browser to sign in, not back yet
        int credits = -1;
        juce::String email, computerCode;
        juce::String lastEmail;           // suggested when signing in again
    };

    ChatPanel();

    void setConversation (const std::vector<StellaAi::Entry>& entries, bool busy);
    void setAccount (const Account& newAccount);

    /** The request, and the files attached to it (images, PDFs, text and code). */
    std::function<void (const juce::String& request, const juce::Array<juce::File>& files)> onSend;
    std::function<void()> onBuyCredits;
    std::function<void()> onRetry;
    std::function<void (const juce::String& email)> onSignIn;   // the email typed, if any
    std::function<void()> onSignOut;
    std::function<void()> onStop;      // Stella AI is working: Send turns into Stop

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;

    // Files dropped on the panel are attached to the next request.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void paintOverChildren (juce::Graphics&) override;
    void fileDragExit (const juce::StringArray& files) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int minimumWidth = 280;

private:
    //==============================================================================
    /** A long, fully rounded yellow button with a black label. */
    class Pill final : public juce::Button
    {
    public:
        explicit Pill (const juce::String& label);
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    };

    enum class BoxMode { editing, showingAccount, waiting };

    void send();
    void signIn();
    void setBoxMode (BoxMode mode);
    void showSuggestion (bool show);
    bool isAccountBoxEvent (const juce::MouseEvent& e) const;
    bool canSuggest() const;
    bool showsWarning() const noexcept;
    juce::String cornerText() const;
    void rebuildTranscript (const std::vector<StellaAi::Entry>& entries);
    void appendAnswer (const juce::String& text);

    /** Round icon buttons inside the request box: attach (a paperclip), send (an arrow) and stop. */
    class IconButton final : public juce::Button
    {
    public:
        enum class Icon { attach, send, stop };

        explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}
        void setIcon (Icon i)    { icon = i; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Icon icon;
    };

    void addAttachment (const juce::File& file);
    void refreshChips();
    void updateSendButton();
    void chooseFiles();

    juce::TextEditor transcript, input;
    juce::TextButton retryButton { "Retry" };
    IconButton sendButton { IconButton::Icon::send }, attachButton { IconButton::Icon::attach };
    juce::Array<juce::File> attachments;
    juce::OwnedArray<juce::TextButton> chips;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> inputBox;
    bool dragOver = false;
    Pill accountPill { "Sign in" }, buyPill { "Buy credits" };
    juce::TextEditor accountBox;

    Account account;
    bool busy = false;
    BoxMode boxMode = BoxMode::editing;
    juce::String typedEmail;   // kept while the box shows something else

    juce::Rectangle<int> titleRow, accountRow, warningArea, bodyArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatPanel)
};
