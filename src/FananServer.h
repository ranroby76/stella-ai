// C:\workspace\Stella AI Studio\src\FananServer.h

#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

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

    A server that was asleep takes a while to wake, so the studio waits for its answer
    rather than giving up early; after a failure it tries again within a second or two, and
    a request that outlives its time is broken off. Every request is noted, with how long it
    took, in connection.log beside account.json.

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
        juce::StringArray internetTestUrls { "http://www.msftconnecttest.com/connecttest.txt",
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
    Response fetch (const juce::URL& url, const juce::String& headers, int timeoutMs, bool isPost, const juce::String& what);
    bool canReachInternet();
    void cancelOverdue();
    static void writeLog (const juce::String& line);
    static juce::String statusName (Status status);
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
    int failedChecks = 0;   // checks in a row that didn't reach the server (message thread)

    /** A request under way, and when it's broken off if it's still waiting. */
    struct Waiting
    {
        juce::WebInputStream* stream = nullptr;
        juce::uint32 deadline = 0;   // in Time::getMillisecondCounter() terms
        bool brokenOff = false;
    };

    juce::CriticalSection streamsLock;
    std::vector<Waiting> activeStreams;

    /** Breaks off requests that outlive their time: Windows doesn't always keep to the
        timeouts it's given (a stalled lookup or handshake can hang for minutes). */
    struct Watchdog final : public juce::Timer
    {
        explicit Watchdog (FananServer& o) : owner (o) {}
        void timerCallback() override   { owner.cancelOverdue(); }
        FananServer& owner;
    };

    Watchdog watchdog { *this };

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // Last, so it's the first to go: its jobs finish while everything above still exists.
    juce::ThreadPool workers { juce::ThreadPoolOptions{}.withThreadName ("Fanan server").withNumberOfThreads (2) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FananServer)
};
