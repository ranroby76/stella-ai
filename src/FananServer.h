// C:\workspace\Stella AI Studio\src\FananServer.h

#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

//==============================================================================
/**
    Stella's link to Fanan's server, which runs Stella AI and keeps the credits.

    Each computer introduces itself with a random key that stays on it, plus a one-way
    hash of the computer's ID (so the free starter credits are given once per computer).
    That's enough to try Stella AI with no account at all.

    To buy credits, or to use the same credits on another computer, the user signs in on
    the Stella site: the studio asks the server for a one-time link, opens it in the
    browser, and once the user is signed in the site links this computer to their Fanan
    account. From then on the computer spends the account's credits; the user never types
    or copies a code. Signing out unlinks the computer again.

    The connection is watched all the time, and "no internet" is told apart from "Fanan's
    server isn't answering", so the user is warned with the right message. Nothing here
    names the AI service behind the server.

    Network waits happen on worker threads; every answer arrives on the message thread.
*/
class FananServer final : private juce::Timer
{
public:
    enum class Status
    {
        checking,       // no answer yet
        online,         // the server answered
        notSetUp,       // the internet works, but this build has no server address yet
        noInternet,     // nothing can be reached
        serverDown      // the internet works, but Fanan's server doesn't answer properly
    };

    struct Settings
    {
        juce::String serverUrl { "https://base44.app" };
        juce::String appId;      // the Stella AI Cloud app on Base44
        juce::String siteUrl;    // the Stella site: /link signs in and links, /buy sells credits
        juce::StringArray internetTestUrls { "https://www.msftconnecttest.com/connecttest.txt",
                                             "https://1.1.1.1/cdn-cgi/trace" };
    };

    /** The built-in settings, overridden by StellaAIStudio-server.json beside the program. */
    static Settings loadSettings();

    using Done = std::function<void (const juce::Result& result, const juce::var& answer)>;

    explicit FananServer (Settings settings);
    ~FananServer() override;

    Status getStatus() const;
    juce::String getStatusText() const;     // one short line for the UI
    int getCredits() const;                 // -1 until the server has said
    juce::String getComputerCode() const;   // this computer's code, for support
    bool isSignedIn() const;                // linked to a Fanan account
    juce::String getEmail() const;          // that account's email
    juce::String getLastEmail() const;      // the email last signed in with on this computer (kept after signing out)

    bool isConfigured() const noexcept      { return settings.appId.isNotEmpty(); }
    bool hasSite() const noexcept           { return settings.siteUrl.isNotEmpty(); }
    bool isReady() const;                   // online, and known to the server

    /** Tests the connection again now (the Retry button). */
    void checkNow();

    /** Opens the Stella site to sign in; the site links this computer to the account.
        emailHint: the email the user typed (the site can fill it in). thenBuy: the site goes
        on to the credits page. done says whether the browser opened. */
    void signIn (bool thenBuy, const juce::String& emailHint, Done done);

    /** Unlinks this computer from its account: it goes back to its own free credits. */
    void signOut (Done done);

    /** The credits page, for a computer that's signed in. */
    juce::URL getStoreUrl() const;

    /** Checks often for a while: a sign-in or purchase in the browser shows up in seconds. */
    void watchForChanges();

    /** Sends one conversation turn to Stella AI. The answer holds { reply, used, credits }. */
    void chat (const juce::var& request, Done done);

    /** Called on the message thread whenever the status, credits or account change. */
    std::function<void()> onStateChanged;

private:
    struct Response
    {
        bool connected = false;   // a server answered at all
        int status = 0;
        juce::var body;
    };

    struct Snapshot
    {
        Status status = Status::checking;
        int credits = -1;
        juce::String code, email;
        bool linked = false;
        bool known = false;   // the server answered "hello"
    };

    void timerCallback() override;
    void startCheck();
    void runCheck();
    void setState (const Snapshot& snapshot);
    void scheduleNextCheck();

    Response post (const juce::String& function, const juce::var& body, int timeoutMs);
    Response fetch (const juce::URL& url, const juce::String& headers, int timeoutMs, bool isPost);
    bool canReachInternet();
    juce::var makeRequest (const juce::String& action) const;
    static juce::String failureText (const Response& response, const juce::String& fallback);

    void deliver (std::function<void()> fn);
    void saveIdentity() const;

    static juce::File getIdentityFile();
    static juce::String makeKey();
    static bool isValidKey (const juce::String& key);
    static juce::String deviceHash();

    const Settings settings;
    juce::String key;   // set once in the constructor, then only read

    mutable juce::CriticalSection stateLock;
    Status status = Status::checking;
    int credits = -1;
    juce::String computerCode, email, lastEmail;
    bool linked = false;

    std::atomic<bool> checking { false };
    int fastChecksLeft = 0;

    juce::CriticalSection streamsLock;
    juce::Array<juce::WebInputStream*> activeStreams;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // Last, so it's the first to go: its jobs finish while everything above still exists.
    juce::ThreadPool workers { juce::ThreadPoolOptions{}.withThreadName ("Fanan server").withNumberOfThreads (2) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FananServer)
};
