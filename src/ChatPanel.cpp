// C:\workspace\Stella AI Studio\src\ChatPanel.cpp

#include "ChatPanel.h"
#include "StellaLookAndFeel.h"

namespace
{
    constexpr int columnWidth      = 820;  // the chat sits in one centred column
    constexpr int headerHeight     = 100;
    constexpr int accountLabelGap  = 58;   // room for the word "Account"
    constexpr int pillWidth        = 150;
    constexpr int warningHeight    = 110;
    constexpr int inputHeight      = 176;

    const juce::Colour pillYellow { 0xffffcc00 };

    juce::String welcomeText()
    {
        return juce::String::fromUTF8 (
            "Describe the plugin you want and Stella AI builds it, playing live while you shape it.\n\n"
            "For example:\n"
            "  \xe2\x80\xa2 a warm tape delay with wow and flutter\n"
            "  \xe2\x80\xa2 a three-oscillator bass synth with a ladder filter\n"
            "  \xe2\x80\xa2 a MIDI arpeggiator that follows the chord\n\n"
            "Then reshape its panel in Edit UI, or click any block in the Schematic and tell "
            "Stella AI what to change in it.");
    }
}

//==============================================================================
ChatPanel::Pill::Pill (const juce::String& label)
    : juce::Button (label)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ChatPanel::Pill::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    auto fill = pillYellow;

    if (! isEnabled())
        fill = fill.withMultipliedAlpha (0.35f);
    else if (isButtonDown)
        fill = fill.darker (0.2f);
    else if (isMouseOver)
        fill = fill.brighter (0.18f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    g.setColour (juce::Colours::black.withAlpha (isEnabled() ? 1.0f : 0.55f));
    g.setFont (Theme::font (14.5f, true));
    g.drawText (getButtonText(), bounds, juce::Justification::centred, false);
}

//==============================================================================
ChatPanel::ChatPanel()
{
    transcript.setMultiLine (true, true);
    transcript.setReadOnly (true);
    transcript.setCaretVisible (false);
    transcript.setScrollbarsShown (true);
    transcript.setColour (juce::TextEditor::backgroundColourId, Theme::panel);
    transcript.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

    input.setMultiLine (true, true);
    input.setReturnKeyStartsNewLine (false);
    input.setFont (Theme::font (15.0f));
    input.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Describe your plugin\xe2\x80\xa6"), Theme::muted);
    input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    input.onReturnKey = [this] { send(); };
    input.onTextChange = [this] { updateSendButton(); };

    attachButton.setTooltip ("Attach files for Stella AI to look at: images, PDFs, text and code (or drop them here)");
    attachButton.onClick = [this] { chooseFiles(); };
    addChildComponent (attachButton);

    sendButton.onClick = [this]
    {
        if (busy)
        {
            if (onStop != nullptr) onStop();
        }
        else
        {
            send();
        }
    };

    retryButton.setTooltip ("Try to connect again now");
    retryButton.onClick = [this] { if (onRetry != nullptr) onRetry(); };

    accountPill.onClick = [this]
    {
        if (account.signedIn)
        {
            if (onSignOut != nullptr) onSignOut();
        }
        else
        {
            signIn();
        }
    };

    buyPill.setTooltip ("Buy Stella AI credits on the Stella site");
    buyPill.onClick = [this] { if (onBuyCredits != nullptr) onBuyCredits(); };

    accountBox.setMultiLine (false);
    accountBox.setFont (Theme::font (14.0f));
    accountBox.setJustification (juce::Justification::centredLeft);
    accountBox.setIndents (8, 0);
    accountBox.setSelectAllWhenFocused (false);
    accountBox.setColour (juce::TextEditor::backgroundColourId, Theme::inset);
    accountBox.setTextToShowWhenEmpty ("Your email", Theme::muted);
    accountBox.onReturnKey = [this] { signIn(); };
    accountBox.addMouseListener (this, true);   // the hover suggestion

    addChildComponent (transcript);
    addChildComponent (input);
    addChildComponent (sendButton);
    addChildComponent (retryButton);
    addChildComponent (buyPill);
    addAndMakeVisible (accountPill);
    addAndMakeVisible (accountBox);

    // Everything starts here: a new plugin, or one to open.
    newPluginButton.setButtonText (juce::String::fromUTF8 ("New plugin\xe2\x80\xa6"));
    newPluginButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    newPluginButton.setTooltip ("Start a new plugin");
    newPluginButton.onClick = [this] { if (onNewPlugin != nullptr) onNewPlugin(); };
    openButton.setButtonText (juce::String::fromUTF8 ("Open\xe2\x80\xa6"));
    openButton.setTooltip ("Open a plugin you're working on");
    openButton.onClick = [this] { if (onOpenPlugin != nullptr) onOpenPlugin(); };
    addAndMakeVisible (newPluginButton);
    addAndMakeVisible (openButton);

    rebuildTranscript ({});
    setAccount ({});
}

//==============================================================================
void ChatPanel::setConversation (const std::vector<StellaAi::Entry>& entries, bool isBusy)
{
    busy = isBusy;
    rebuildTranscript (entries);

    // Typing ahead is fine while Stella AI works; the button stops it instead of sending.
    updateSendButton();
}

void ChatPanel::updateSendButton()
{
    sendButton.setIcon (busy ? IconButton::Icon::stop : IconButton::Icon::send);
    sendButton.setEnabled (busy || input.getText().trim().isNotEmpty() || ! attachments.isEmpty());
    sendButton.setTooltip (busy ? "Stop Stella AI after the step it's doing" : juce::String ("Send (Enter)"));
}

//==============================================================================
void ChatPanel::IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre()).reduced (1.0f);
    const auto centre = circle.getCentre();
    const auto on = isEnabled();

    if (icon == Icon::attach)
    {
        g.setColour (highlighted ? Theme::raised.brighter (0.1f) : juce::Colours::transparentBlack);
        g.fillEllipse (circle);

        // A paperclip: a tall loop with a shorter one inside, tilted.
        juce::Path clip;
        const auto h = size * 0.56f, w = size * 0.24f;
        clip.addRoundedRectangle (-w * 0.5f, -h * 0.5f, w, h, w * 0.5f);
        clip.startNewSubPath (0.0f, -h * 0.28f);
        clip.lineTo (0.0f, h * 0.22f);
        clip.applyTransform (juce::AffineTransform::rotation (0.6f).translated (centre));

        g.setColour (down ? Theme::accent : (highlighted ? Theme::text : Theme::muted));
        g.strokePath (clip, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    g.setColour (on ? (down ? Theme::accent.darker (0.2f) : (highlighted ? Theme::accent.brighter (0.15f) : Theme::accent))
                    : Theme::raised);
    g.fillEllipse (circle);

    g.setColour (on ? juce::Colours::white : Theme::muted);

    if (icon == Icon::stop)
    {
        g.fillRoundedRectangle (juce::Rectangle<float> (size * 0.32f, size * 0.32f).withCentre (centre), 2.0f);
        return;
    }

    // An arrow pointing up: send.
    juce::Path arrow;
    const auto a = size * 0.22f;
    arrow.startNewSubPath (centre.x, centre.y + a);
    arrow.lineTo (centre.x, centre.y - a);
    arrow.startNewSubPath (centre.x - a * 0.8f, centre.y - a * 0.2f);
    arrow.lineTo (centre.x, centre.y - a);
    arrow.lineTo (centre.x + a * 0.8f, centre.y - a * 0.2f);
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void ChatPanel::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Attach files for Stella AI", juce::File(), StellaAi::attachableFiles());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safeThis = juce::Component::SafePointer<ChatPanel> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis != nullptr)
                                  for (const auto& file : fc.getResults())
                                      safeThis->addAttachment (file);
                          });
}

void ChatPanel::addAttachment (const juce::File& file)
{
    if (! StellaAi::canAttach (file) || attachments.contains (file) || attachments.size() >= StellaAi::maxAttachments)
        return;

    attachments.add (file);
    refreshChips();
}

void ChatPanel::refreshChips()
{
    chips.clear();

    for (const auto& file : attachments)
    {
        auto* chip = chips.add (new juce::TextButton (file.getFileName() + juce::String::fromUTF8 ("  \xc3\x97")));
        chip->setTooltip (file.getFullPathName() + "\nClick to remove");
        chip->setColour (juce::TextButton::buttonColourId, Theme::raised);
        chip->onClick = [this, file]
        {
            attachments.removeFirstMatchingValue (file);
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ChatPanel> (this)]
            {
                if (safeThis != nullptr)
                    safeThis->refreshChips();
            });
        };
        addAndMakeVisible (chip);
    }

    updateSendButton();
    resized();
    repaint();
}

bool ChatPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! account.signedIn)
        return false;

    for (const auto& path : files)
        if (StellaAi::canAttach (juce::File (path)))
            return true;

    return false;
}

void ChatPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;

    for (const auto& path : files)
        addAttachment (juce::File (path));

    repaint();
}

void ChatPanel::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver || inputBox.isEmpty())
        return;

    g.setColour (Theme::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (inputBox.toFloat().reduced (2.0f), 7.0f);
    g.setColour (Theme::accent);
    g.drawRoundedRectangle (inputBox.toFloat().reduced (1.0f), 8.0f, 2.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawText ("Drop to attach", inputBox, juce::Justification::centred, false);
}

void ChatPanel::fileDragMove (const juce::StringArray&, int, int)
{
    if (! dragOver)
    {
        dragOver = true;
        repaint();
    }
}

void ChatPanel::fileDragEnter (const juce::StringArray&, int, int)    { dragOver = true; repaint(); }
void ChatPanel::fileDragExit (const juce::StringArray&)               { dragOver = false; repaint(); }

void ChatPanel::setAccount (const Account& newAccount)
{
    account = newAccount;

    const bool online = account.status == FananServer::Status::online;

    // The account box: the signed-in email, the wait for the browser, or an email to sign in with.
    setBoxMode (account.signedIn ? BoxMode::showingAccount
              : account.waitingForBrowser ? BoxMode::waiting
                                          : BoxMode::editing);

    if (boxMode == BoxMode::showingAccount)
        accountBox.setText (account.email.isNotEmpty() ? account.email : juce::String ("Signed in"), juce::dontSendNotification);

    accountBox.setTooltip (account.computerCode.isNotEmpty() ? "This computer: " + account.computerCode : juce::String());

    // The pills: Sign in alone, or Sign out and Buy credits side by side.
    accountPill.setButtonText (account.signedIn ? "Sign out" : "Sign in");
    accountPill.setTooltip (account.signedIn ? "Sign this computer out. Your credits stay with your account."
                                             : "Sign in on the Stella site: your account and credits work on every computer");
    accountPill.setEnabled (online && account.siteAvailable);

    buyPill.setVisible (account.signedIn && account.siteAvailable);
    buyPill.setEnabled (online);

    // Stella AI itself is there only once signed in.
    transcript.setVisible (account.signedIn);
    input.setVisible (account.signedIn);
    sendButton.setVisible (account.signedIn);
    attachButton.setVisible (account.signedIn);

    for (auto* chip : chips)
        chip->setVisible (account.signedIn);

    retryButton.setVisible (showsWarning());

    resized();
    repaint();
}

void ChatPanel::setBoxMode (BoxMode mode)
{
    if (mode == boxMode)
        return;   // checks run every few minutes: what the user typed is left alone

    if (boxMode == BoxMode::editing)
        typedEmail = accountBox.getText().trim();

    boxMode = mode;

    const bool editing = mode == BoxMode::editing;

    accountBox.setReadOnly (! editing);
    accountBox.setCaretVisible (editing);
    accountBox.setMouseCursor (editing ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);

    switch (mode)
    {
        case BoxMode::editing:
            accountBox.setText (typedEmail, juce::dontSendNotification);
            accountBox.setColour (juce::TextEditor::textColourId, Theme::text);
            accountBox.setColour (juce::TextEditor::outlineColourId, Theme::outline);
            break;

        case BoxMode::showingAccount:
            typedEmail.clear();   // after signing out, the box starts empty (with the suggestion)
            accountBox.setColour (juce::TextEditor::textColourId, Theme::text);
            accountBox.setColour (juce::TextEditor::outlineColourId, pillYellow.withAlpha (0.55f));
            break;

        case BoxMode::waiting:
            accountBox.setText (juce::String::fromUTF8 ("Finish in your browser\xe2\x80\xa6"), juce::dontSendNotification);
            accountBox.setColour (juce::TextEditor::textColourId, Theme::hot);
            accountBox.setColour (juce::TextEditor::outlineColourId, Theme::outline);
            break;
    }

    // TextEditor applies a new text colour to text added from now on; recolour what's there.
    accountBox.applyColourToAllText (accountBox.findColour (juce::TextEditor::textColourId), true);
    showSuggestion (false);
}

bool ChatPanel::canSuggest() const
{
    return boxMode == BoxMode::editing && accountBox.isEmpty() && account.lastEmail.isNotEmpty();
}

void ChatPanel::showSuggestion (bool show)
{
    // The email last signed in with, shown in the empty box while the mouse is over it.
    if (show && canSuggest())
        accountBox.setTextToShowWhenEmpty (account.lastEmail, pillYellow.withAlpha (0.75f));
    else
        accountBox.setTextToShowWhenEmpty ("Your email", Theme::muted);

    accountBox.repaint();
}

bool ChatPanel::isAccountBoxEvent (const juce::MouseEvent& e) const
{
    return e.eventComponent == &accountBox || accountBox.isParentOf (e.eventComponent);
}

void ChatPanel::mouseEnter (const juce::MouseEvent& e)
{
    if (isAccountBoxEvent (e))
        showSuggestion (true);
}

void ChatPanel::mouseExit (const juce::MouseEvent& e)
{
    if (isAccountBoxEvent (e) && ! accountBox.getScreenBounds().contains (e.getScreenPosition()))
        showSuggestion (false);
}

void ChatPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! isAccountBoxEvent (e) || ! canSuggest())
        return;

    // A click takes the suggestion: filled in and selected, so typing replaces it.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ChatPanel> (this)]
    {
        if (safeThis == nullptr || ! safeThis->canSuggest())
            return;

        safeThis->accountBox.setText (safeThis->account.lastEmail, juce::dontSendNotification);
        safeThis->accountBox.selectAll();
        safeThis->showSuggestion (false);
    });
}

void ChatPanel::signIn()
{
    if (account.signedIn || ! accountPill.isEnabled())
        return;

    const auto typed = boxMode == BoxMode::editing ? accountBox.getText().trim() : typedEmail;

    if (onSignIn != nullptr)
        onSignIn (typed);
}

bool ChatPanel::showsWarning() const noexcept
{
    return account.status == FananServer::Status::noInternet || account.status == FananServer::Status::serverDown;
}

juce::String ChatPanel::cornerText() const
{
    switch (account.status)
    {
        case FananServer::Status::checking:    return juce::String::fromUTF8 ("Connecting\xe2\x80\xa6");
        case FananServer::Status::noInternet:  return "Offline";
        case FananServer::Status::serverDown:  return "Server not responding";
        case FananServer::Status::notSetUp:    return "Not set up";
        case FananServer::Status::online:
            if (! account.signedIn || account.credits < 0)
                return "Online";

            return juce::String::fromUTF8 ("Online \xc2\xb7 ") + juce::String (account.credits)
                 + (account.credits == 1 ? " credit" : " credits");
    }

    return {};
}

void ChatPanel::send()
{
    const auto text = input.getText().trim();

    if (busy || ! account.signedIn || (text.isEmpty() && attachments.isEmpty()))
        return;

    const auto files = attachments;
    input.clear();
    attachments.clear();
    refreshChips();

    if (onSend != nullptr)
        onSend (text, files);
}

//==============================================================================
//==============================================================================
namespace
{
    /** A stretch of an answer in one style. */
    struct Run
    {
        juce::String text;
        bool bold = false, italic = false, code = false, ask = false;
    };

    /** Inline marks, each only when it's closed on the same line: **bold**, *italic*,
        `code`, and ==a question or a task for the user== (shown in yellow). */
    juce::Array<Run> parseLine (const juce::String& line, bool bold)
    {
        juce::Array<Run> runs;
        bool italic = false, code = false, ask = false;
        Run current;
        current.bold = bold;

        auto flush = [&]
        {
            if (current.text.isNotEmpty())
                runs.add (current);

            current = Run();
            current.bold = bold;
            current.italic = italic;
            current.code = code;
            current.ask = ask;
        };

        const auto n = line.length();

        for (int i = 0; i < n;)
        {
            const auto rest = line.substring (i);
            auto closedLater = [&line] (const char* mark, int from) { return line.indexOf (from, mark) >= 0; };

            if (! code && rest.startsWith ("**") && (bold || closedLater ("**", i + 2)))
            {
                flush();
                bold = ! bold;
                current.bold = bold;
                i += 2;
            }
            else if (! code && rest.startsWith ("==") && (ask || closedLater ("==", i + 2)))
            {
                flush();
                ask = ! ask;
                current.ask = ask;
                i += 2;
            }
            else if (line[i] == '`' && (code || closedLater ("`", i + 1)))
            {
                flush();
                code = ! code;
                current.code = code;
                i += 1;
            }
            else if (! code && line[i] == '*' && (italic || (i + 1 < n && line[i + 1] != ' ' && closedLater ("*", i + 1))))
            {
                flush();
                italic = ! italic;
                current.italic = italic;
                i += 1;
            }
            else
            {
                current.text += juce::String::charToString (line[i]);
                ++i;
            }
        }

        flush();
        return runs;
    }

    /** When an answer marks nothing for the user, its questions are found by their "?". */
    juce::String markQuestions (const juce::String& line)
    {
        juce::String result, sentence;

        for (int i = 0; i < line.length(); ++i)
        {
            sentence += juce::String::charToString (line[i]);
            const auto end = line[i] == '.' || line[i] == '!' || line[i] == '?';

            if (end && (i + 1 == line.length() || line[i + 1] == ' '))
            {
                const auto trimmed = sentence.trim();
                result += line[i] == '?' && trimmed.isNotEmpty()
                              ? sentence.substring (0, sentence.length() - trimmed.length()) + "==" + trimmed + "=="
                              : sentence;
                sentence.clear();
            }
        }

        return result + sentence;
    }
}

void ChatPanel::appendAnswer (const juce::String& text)
{
    // Markdown-lite: bold and italic as fonts, bullets as dots, headings bold, and what
    // the user should answer or do in yellow. The marks themselves never show.
    // Code never shows: a fenced block is left out, whatever Stella AI wrote.
    juce::StringArray lines;
    bool inCode = false;

    for (const auto& line : juce::StringArray::fromLines (text))
    {
        if (line.trimStart().startsWith ("```"))
            inCode = ! inCode;
        else if (! inCode)
            lines.add (line);
    }

    while (! lines.isEmpty() && lines[lines.size() - 1].trim().isEmpty())
        lines.remove (lines.size() - 1);

    const bool marked = text.contains ("==");

    for (int l = 0; l < lines.size(); ++l)
    {
        auto line = lines[l];
        const auto trimmed = line.trimStart();
        juce::String prefix;
        bool heading = false;

        if (trimmed == "---" || trimmed == "***" || trimmed == "___")
        {
            line.clear();
        }
        else if (trimmed.startsWith ("#"))
        {
            line = trimmed.trimCharactersAtStart ("#").trimStart();
            heading = true;
        }
        else if (trimmed.startsWith ("- ") || trimmed.startsWith ("* ") || trimmed.startsWith (juce::String::fromUTF8 ("\xe2\x80\xa2 ")))
        {
            prefix = juce::String::repeatedString (" ", juce::jmin (6, line.length() - trimmed.length())) + juce::String::fromUTF8 ("\xe2\x80\xa2 ");
            line = trimmed.substring (2);
        }
        else if (trimmed.startsWith ("> "))
        {
            line = trimmed.substring (2);
        }

        if (! marked)
            line = markQuestions (line);

        if (prefix.isNotEmpty())
        {
            transcript.setFont (Theme::font (15.0f));
            transcript.setColour (juce::TextEditor::textColourId, Theme::muted);
            transcript.insertTextAtCaret (prefix);
        }

        for (const auto& run : parseLine (line, heading))
        {
            auto font = Theme::font (15.0f, run.bold);   // `marked` words read as plain words

            if (run.italic)
                font = font.italicised();

            transcript.setFont (font);
            transcript.setColour (juce::TextEditor::textColourId, run.ask ? pillYellow : Theme::text.withAlpha (0.9f));
            transcript.insertTextAtCaret (run.text);
        }

        if (l + 1 < lines.size())
            transcript.insertTextAtCaret ("\n");
    }
}

void ChatPanel::rebuildTranscript (const std::vector<StellaAi::Entry>& entries)
{
    transcript.clear();

    auto add = [this] (const juce::String& text, juce::Colour colour, bool bold)
    {
        transcript.setFont (Theme::font (15.0f, bold));
        transcript.setColour (juce::TextEditor::textColourId, colour);
        transcript.insertTextAtCaret (text);
    };

    if (entries.empty())
        add (welcomeText(), Theme::text.withAlpha (0.88f), false);

    for (const auto& entry : entries)
    {
        switch (entry.kind)
        {
            case StellaAi::Entry::Kind::user:
                add ("You\n", Theme::accent, true);
                add (entry.text + "\n\n", Theme::text, false);
                break;

            case StellaAi::Entry::Kind::ai:
                add ("Stella AI\n", Theme::text, true);
                appendAnswer (entry.text);
                add ("\n\n", Theme::text, false);
                break;

            case StellaAi::Entry::Kind::notice:
                add (entry.text + "\n\n", Theme::hot, false);
                break;

            case StellaAi::Entry::Kind::activity:
                transcript.setFont (Theme::font (13.0f));
                transcript.setColour (juce::TextEditor::textColourId, Theme::muted);
                transcript.insertTextAtCaret (juce::String::fromUTF8 ("\xe2\x80\xba ") + entry.text + "\n\n");
                break;
        }
    }

    if (busy)
        add (juce::String::fromUTF8 ("Stella AI is working\xe2\x80\xa6\n"), Theme::muted, false);

    transcript.moveCaretToEnd();
}

//==============================================================================
void ChatPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    if (account.signedIn && ! inputBox.isEmpty())
    {
        g.setColour (Theme::inset);
        g.fillRoundedRectangle (inputBox.toFloat(), 8.0f);
        g.setColour (input.hasKeyboardFocus (true) ? Theme::accent.withAlpha (0.6f) : Theme::outline);
        g.drawRoundedRectangle (inputBox.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    if (dragOver && ! inputBox.isEmpty())
    {
        // Files dragged over the chat: the request box says where they go.
        g.setColour (Theme::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (inputBox.toFloat(), 8.0f);
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (inputBox.toFloat().reduced (1.0f), 8.0f, 2.0f);
    }

    // Title on the left; the connection light and the credits in the top right corner.
    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText ("Stella AI", titleRow, juce::Justification::centredLeft, false);

    {
        const auto text = cornerText();
        const auto font = Theme::font (13.0f);

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        const auto textWidth = juce::jmin ((float) (titleRow.getRight() - openButton.getRight()) - 40.0f,
                                           glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true).getWidth() + 2.0f);

        auto corner = titleRow.toFloat();
        const auto textArea = corner.removeFromRight (textWidth);
        corner.removeFromRight (6.0f);
        const auto light = corner.removeFromRight (8.0f).withSizeKeepingCentre (8.0f, 8.0f);

        const auto lightColour = account.status == FananServer::Status::online   ? Theme::safe
                               : account.status == FananServer::Status::notSetUp ? Theme::hot
                               : showsWarning()                                  ? Theme::clip
                                                                                 : Theme::muted;
        g.setColour (lightColour.withAlpha (0.35f));
        g.fillEllipse (light.expanded (2.5f));   // a soft glow, like a real LED
        g.setColour (lightColour);
        g.fillEllipse (light);

        g.setColour (showsWarning() ? Theme::clip.brighter (0.4f) : Theme::text.withAlpha (0.85f));
        g.setFont (font);
        g.drawText (text, textArea, juce::Justification::centredRight, true);
    }

    g.setColour (Theme::muted);
    g.setFont (Theme::font (13.0f));
    g.drawText ("Account", accountRow.withWidth (accountLabelGap), juce::Justification::centredLeft, false);

    g.setColour (Theme::outline);
    g.fillRect (0, headerHeight, getWidth(), 1);

    // The warning: it stays until the studio is back online.
    if (showsWarning() && ! warningArea.isEmpty())
    {
        const auto box = warningArea.toFloat();

        g.setColour (Theme::clip.withAlpha (0.14f));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (Theme::clip.withAlpha (0.75f));
        g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);

        auto text = warningArea.reduced (12, 9);
        text.removeFromBottom (retryButton.getHeight() + 4);

        const bool noInternet = account.status == FananServer::Status::noInternet;

        g.setColour (Theme::text);
        g.setFont (Theme::font (14.5f, true));
        g.drawText (noInternet ? "No internet connection" : "Fanan's server isn't responding",
                    text.removeFromTop (18), juce::Justification::centredLeft, true);

        g.setColour (Theme::text.withAlpha (0.8f));
        g.setFont (Theme::font (13.0f));
        g.drawFittedText (noInternet ? "Stella AI needs the internet. Everything else keeps working."
                                     : "Your internet works. Stella keeps trying on its own.",
                          text, juce::Justification::topLeft, 2, 1.0f);
    }

    // Until the user signs in, this is all the body shows.
    if (! account.signedIn && ! bodyArea.isEmpty())
    {
        g.setColour (Theme::text.withAlpha (0.85f));
        g.setFont (Theme::font (16.0f));
        g.drawFittedText ("Sign in to start using Stella AI", bodyArea.reduced (20), juce::Justification::centred, 2, 1.0f);
    }
}

void ChatPanel::focusInput()
{
    if (input.isShowing())
        input.grabKeyboardFocus();
}

void ChatPanel::resized()
{
    // One centred column, like a chat page: the header, the conversation, the request box.
    auto area = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 32, columnWidth), getHeight());

    auto header = area.removeFromTop (headerHeight);
    header.removeFromTop (14);
    titleRow = header.removeFromTop (30);
    header.removeFromTop (10);

    // New plugin and Open, beside the title.
    auto starts = titleRow.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (Theme::font (16.0f, true), "Stella AI") + 20);
    newPluginButton.setBounds (starts.removeFromLeft (118));
    starts.removeFromLeft (8);
    openButton.setBounds (starts.removeFromLeft (84));

    // The account box, with Sign in / Sign out and Buy credits beside it.
    auto row = header.removeFromTop (32);
    auto pills = row.removeFromRight (buyPill.isVisible() ? pillWidth * 2 + 10 : pillWidth);

    if (buyPill.isVisible())
    {
        accountPill.setBounds (pills.removeFromLeft (pillWidth));
        pills.removeFromLeft (10);
        buyPill.setBounds (pills);
    }
    else
    {
        accountPill.setBounds (pills);
    }

    row.removeFromRight (16);
    accountRow = row.withSizeKeepingCentre (row.getWidth(), 26);
    accountBox.setBounds (accountRow.withTrimmedLeft (accountLabelGap));

    area.removeFromTop (1);   // the line under the header

    if (showsWarning())
    {
        warningArea = area.removeFromTop (warningHeight).reduced (0, 10);
        retryButton.setBounds (warningArea.reduced (10, 8).removeFromBottom (26).removeFromRight (76));
    }
    else
    {
        warningArea = {};
    }

    bodyArea = area;

    if (account.signedIn)
    {
        // Attached files as chips above the request box; the attach and send buttons inside it.
        const auto chipRow = chips.isEmpty() ? 0 : 30;
        auto bottom = area.removeFromBottom (inputHeight + chipRow).withTrimmedTop (8).withTrimmedBottom (18);

        if (chipRow > 0)
        {
            auto chipLine = bottom.removeFromTop (24);

            for (auto* chip : chips)
            {
                const auto width = juce::jmin (170, juce::GlyphArrangement::getStringWidthInt (Theme::font (13.0f), chip->getButtonText()) + 24);
                chip->setBounds (chipLine.removeFromLeft (juce::jmin (width, chipLine.getWidth())));
                chipLine.removeFromLeft (6);
            }

            bottom.removeFromTop (6);
        }

        inputBox = bottom;
        auto buttons = bottom.removeFromBottom (40).reduced (8, 4);
        attachButton.setBounds (buttons.removeFromLeft (32));
        sendButton.setBounds (buttons.removeFromRight (32));
        input.setBounds (bottom.reduced (8, 6));

        transcript.setBounds (area.withTrimmedTop (10).withTrimmedBottom (4));

        // A warning appearing shrinks the conversation: keep its newest lines in view.
        transcript.moveCaretToEnd();
    }
}
