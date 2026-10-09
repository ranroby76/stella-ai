// C:\workspace\Stella AI Studio\src\FananServer.cpp

#include "FananServer.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <iterator>
#include <random>

namespace
{
    // The Stella AI Cloud app on Base44 and its site. StellaAIStudio-server.json beside the
    // program can name others (for testing against another server).
    constexpr const char* builtInAppId   = "6ac03f5bd8e7cddad8252682";
    constexpr const char* builtInSiteUrl = "https://rugged-stella-studio-flow.base44.app";

    // How long to wait for an answer. A server that was asleep takes a while to wake: the
    // studio waits for it, rather than giving up early and asking again (which can keep it
    // from ever waking).
    constexpr int helloTimeoutMs    = 30000;
    constexpr int accountTimeoutMs  = 20000;    // signing in and out
    constexpr int internetTimeoutMs = 5000;     // the internet test, only after a failure
    constexpr int chatTimeoutMs     = 320000;   // a building step can take minutes: Base44 allows 5, plus a margin
    constexpr int overdueMarginMs   = 5000;     // past its timeout plus this, a request is broken off

    // When to check again.
    constexpr int onlineCheckMs  = 120000;
    constexpr int offlineCheckMs = 10000;
    constexpr int retryMs[]      = { 1000, 2000, 4000, 8000 };   // the first tries after a failure; then offlineCheckMs
    constexpr int watchCheckMs   = 6000;
    constexpr int watchChecks    = 100;      // ten minutes: signing in and paying take a while

    constexpr juce::int64 logLimitBytes = 256 * 1024;   // past this, the older half of the log goes

    juce::var object()
    {
        return juce::var (new juce::DynamicObject());
    }
}

//==============================================================================
FananServer::Settings FananServer::loadSettings()
{
    Settings loaded;
    loaded.appId = builtInAppId;
    loaded.siteUrl = builtInSiteUrl;

    const auto config = juce::JSON::parse (juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                               .getSiblingFile ("StellaAIStudio-server.json"));

    if (const auto value = config.getProperty ("serverUrl", {}).toString().trim(); value.isNotEmpty())
        loaded.serverUrl = value.trimCharactersAtEnd ("/");

    if (const auto value = config.getProperty ("appId", {}).toString().trim(); value.isNotEmpty())
        loaded.appId = value;

    if (const auto value = config.getProperty ("siteUrl", {}).toString().trim(); value.isNotEmpty())
        loaded.siteUrl = value.trimCharactersAtEnd ("/");
    else if (const auto store = config.getProperty ("storeUrl", {}).toString().trim(); store.isNotEmpty())
        loaded.siteUrl = store.trimCharactersAtEnd ("/").upToLastOccurrenceOf ("/buy", false, true);   // the older setting

    if (const auto* list = config.getProperty ("internetTestUrls", {}).getArray())
    {
        loaded.internetTestUrls.clear();

        for (const auto& address : *list)
            if (address.toString().trim().isNotEmpty())
                loaded.internetTestUrls.add (address.toString().trim());
    }

    return loaded;
}

//==============================================================================
FananServer::FananServer (Settings s)
    : settings (std::move (s))
{
    const auto saved = juce::JSON::parse (getIdentityFile());

    key = saved.getProperty ("key", {}).toString();
    computerCode = saved.getProperty ("account", {}).toString();
    lastEmail = saved.getProperty ("lastEmail", {}).toString();

    // The key is made once and kept, so this computer stays known to the server.
    if (! isValidKey (key))
    {
        key = makeKey();
        computerCode.clear();
        saveIdentity();
    }

    writeLog (juce::String ("Stella AI Studio ") + JUCE_APPLICATION_VERSION_STRING + " started");

    watchdog.startTimer (1000);
    startCheck();
}

FananServer::~FananServer()
{
    stopTimer();
    watchdog.stopTimer();
    alive->store (false);

    // Waiting requests are broken off, so quitting never hangs on the network.
    {
        const juce::ScopedLock lock (streamsLock);

        for (auto& waiting : activeStreams)
            waiting.stream->cancel();
    }

    workers.removeAllJobs (true, 4000);
}

//==============================================================================
juce::File FananServer::getIdentityFile()
{
    // In the user's application data, not beside the program: reinstalling or updating the
    // studio keeps the computer known (and signed in), and remembers the last email used.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Fanan")
               .getChildFile ("Stella AI Studio")
               .getChildFile ("account.json");
}

void FananServer::saveIdentity() const
{
    auto saved = object();
    saved.getDynamicObject()->setProperty ("key", key);
    saved.getDynamicObject()->setProperty ("account", getComputerCode());
    saved.getDynamicObject()->setProperty ("lastEmail", getLastEmail());

    const auto file = getIdentityFile();
    file.getParentDirectory().createDirectory();

    juce::TemporaryFile temp (file);

    if (temp.getFile().replaceWithText (juce::JSON::toString (saved)))
        temp.overwriteTargetFileWithTemporary();
}

juce::String FananServer::makeKey()
{
    // 256 bits from the system's secure random source.
    std::random_device source;
    juce::String hex;

    for (int i = 0; i < 8; ++i)
        hex << juce::String::toHexString ((juce::uint32) source()).paddedLeft ('0', 8);

    return hex.toLowerCase();
}

bool FananServer::isValidKey (const juce::String& candidate)
{
    return candidate.length() == 64 && candidate.containsOnly ("0123456789abcdef");
}

juce::String FananServer::deviceHash()
{
    // Only a one-way hash leaves the computer, never the ID itself.
    const auto id = juce::SystemStats::getUniqueDeviceID();

    if (id.isEmpty())
        return {};

    return juce::SHA256 (("stella-device:" + id).toUTF8()).toHexString();
}

juce::var FananServer::makeRequest (const juce::String& action) const
{
    auto body = object();
    body.getDynamicObject()->setProperty ("action", action);
    body.getDynamicObject()->setProperty ("token", key);
    return body;
}

//==============================================================================
FananServer::Status FananServer::getStatus() const
{
    const juce::ScopedLock lock (stateLock);
    return status;
}

int FananServer::getCredits() const
{
    const juce::ScopedLock lock (stateLock);
    return credits;
}

juce::String FananServer::getComputerCode() const
{
    const juce::ScopedLock lock (stateLock);
    return computerCode;
}

bool FananServer::isSignedIn() const
{
    const juce::ScopedLock lock (stateLock);
    return linked;
}

juce::String FananServer::getEmail() const
{
    const juce::ScopedLock lock (stateLock);
    return email;
}

juce::String FananServer::getLastEmail() const
{
    const juce::ScopedLock lock (stateLock);
    return lastEmail;
}

bool FananServer::isReady() const
{
    const juce::ScopedLock lock (stateLock);
    return status == Status::online && computerCode.isNotEmpty();
}

juce::String FananServer::getStatusText() const
{
    const juce::ScopedLock lock (stateLock);

    switch (status)
    {
        case Status::checking:    return juce::String::fromUTF8 ("Connecting\xe2\x80\xa6");
        case Status::notSetUp:    return "Online, but Stella AI isn't set up in this build yet";
        case Status::noInternet:  return "No internet connection";
        case Status::serverDown:  return "Fanan's server isn't responding";
        case Status::online:
            // Credits belong to the account (the free ones come with the first sign-in):
            // before signing in there's no balance to show.
            if (credits < 0 || ! linked)
                return "Online";

            return juce::String::fromUTF8 ("Online \xc2\xb7 ") + juce::String (credits) + (credits == 1 ? " credit" : " credits");
    }

    return {};
}

juce::URL FananServer::getStoreUrl() const
{
    return hasSite() ? juce::URL (settings.siteUrl + "/buy") : juce::URL();
}

//==============================================================================
void FananServer::checkNow()
{
    stopTimer();

    {
        const juce::ScopedLock lock (stateLock);

        if (status != Status::online)
            status = Status::checking;
    }

    if (onStateChanged != nullptr)
        onStateChanged();

    startCheck();
}

void FananServer::watchForChanges()
{
    fastChecksLeft = watchChecks;

    if (! checking.load())
        startTimer (watchCheckMs);
}

void FananServer::timerCallback()
{
    stopTimer();
    startCheck();
}

void FananServer::startCheck()
{
    if (checking.exchange (true))
        return;   // one is under way; its answer schedules the next

    workers.addJob ([this, stillAlive = alive]
    {
        if (stillAlive->load())
            runCheck();
    });
}

void FananServer::runCheck()
{
    Snapshot snapshot;

    if (! isConfigured())
    {
        snapshot.status = canReachInternet() ? Status::notSetUp : Status::noInternet;
    }
    else
    {
        // "hello" makes this computer known the first time, and reports its credits and
        // account (if it's signed in) after that.
        auto body = makeRequest ("hello");
        body.getDynamicObject()->setProperty ("device", deviceHash());
        body.getDynamicObject()->setProperty ("app", juce::String ("Stella AI Studio ") + JUCE_APPLICATION_VERSION_STRING);

        const auto response = post ("stellaAccount", body, helloTimeoutMs);

        if (response.connected && response.status == 200 && (bool) response.body.getProperty ("ok", false))
        {
            snapshot.status = Status::online;
            snapshot.known = true;
            snapshot.credits = juce::jmax (0, (int) response.body.getProperty ("credits", 0));
            snapshot.code = response.body.getProperty ("account", {}).toString();
            snapshot.linked = (bool) response.body.getProperty ("linked", false);
            snapshot.email = response.body.getProperty ("email", {}).toString();
        }
        else if (response.connected)
        {
            snapshot.status = Status::serverDown;   // it answered, but not properly
        }
        else
        {
            snapshot.status = canReachInternet() ? Status::serverDown : Status::noInternet;
        }
    }

    deliver ([this, snapshot]
    {
        checking.store (false);
        setState (snapshot);
    });
}

void FananServer::setState (const Snapshot& snapshot)
{
    bool identityChanged = false, statusChanged = false;
    const bool reached = snapshot.status == Status::online || snapshot.status == Status::notSetUp;

    failedChecks = reached ? 0 : failedChecks + 1;

    {
        const juce::ScopedLock lock (stateLock);

        statusChanged = status != snapshot.status;
        status = snapshot.status;

        if (snapshot.known)
        {
            credits = snapshot.credits;
            linked = snapshot.linked;
            email = snapshot.linked ? snapshot.email : juce::String();

            if (snapshot.code.isNotEmpty() && snapshot.code != computerCode)
            {
                computerCode = snapshot.code;
                identityChanged = true;
            }

            // Remembered for next time: after signing out, the studio suggests it.
            if (email.isNotEmpty() && email != lastEmail)
            {
                lastEmail = email;
                identityChanged = true;
            }
        }
    }

    if (identityChanged)
        saveIdentity();

    if (statusChanged)
        writeLog ("Now " + statusName (snapshot.status));

    scheduleNextCheck();

    if (onStateChanged != nullptr)
        onStateChanged();
}

void FananServer::scheduleNextCheck()
{
    if (fastChecksLeft > 0)
    {
        --fastChecksLeft;
        startTimer (watchCheckMs);
        return;
    }

    const auto current = getStatus();

    if (current == Status::online || current == Status::notSetUp)
    {
        startTimer (onlineCheckMs);
        return;
    }

    // Not reached: again in a second or two, then every few seconds.
    const auto quickTries = (int) std::size (retryMs);
    startTimer (failedChecks >= 1 && failedChecks <= quickTries ? retryMs[failedChecks - 1] : offlineCheckMs);
}

juce::String FananServer::statusName (Status s)
{
    switch (s)
    {
        case Status::checking:    return "connecting";
        case Status::online:      return "online";
        case Status::notSetUp:    return "online, not set up";
        case Status::noInternet:  return "no internet";
        case Status::serverDown:  return "the server isn't answering";
    }

    return {};
}

//==============================================================================
juce::String FananServer::failureText (const Response& response, const juce::String& fallback)
{
    if (! response.connected)
        return "The studio couldn't reach Fanan's server. Check the internet connection and try again.";

    const auto message = response.body.getProperty ("message", {}).toString().trim();
    return message.isNotEmpty() ? message : fallback;
}

void FananServer::signIn (bool thenBuy, const juce::String& emailHint, Done done)
{
    if (! hasSite() || ! isConfigured())
    {
        if (done != nullptr)
            done (juce::Result::fail ("Signing in isn't set up in this build yet."), {});

        return;
    }

    workers.addJob ([this, stillAlive = alive, thenBuy, hint = emailHint.trim(), done]
    {
        if (! stillAlive->load())
            return;

        // A one-time link: it works once, for a few minutes, and only for this computer.
        const auto response = post ("stellaAccount", makeRequest ("link"), accountTimeoutMs);
        const auto linkToken = response.body.getProperty ("link", {}).toString();

        auto result = juce::Result::ok();

        if (! response.connected || response.status != 200 || ! (bool) response.body.getProperty ("ok", false) || linkToken.isEmpty())
            result = juce::Result::fail (failureText (response, "Signing in isn't available right now. Please try again."));

        deliver ([this, result, linkToken, thenBuy, hint, done]
        {
            auto outcome = result;

            if (outcome.wasOk())
            {
                auto url = juce::URL (settings.siteUrl + "/link").withParameter ("t", linkToken);

                if (hint.containsChar ('@'))
                    url = url.withParameter ("email", hint);   // the site can fill it in

                if (thenBuy)
                    url = url.withParameter ("next", "buy");

                if (url.launchInDefaultBrowser())
                    watchForChanges();   // the account shows up here within seconds of signing in
                else
                    outcome = juce::Result::fail ("The web browser couldn't be opened.");
            }

            if (done != nullptr)
                done (outcome, {});
        });
    });
}

void FananServer::signOut (Done done)
{
    workers.addJob ([this, stillAlive = alive, done]
    {
        if (! stillAlive->load())
            return;

        const auto response = post ("stellaAccount", makeRequest ("unlink"), accountTimeoutMs);

        auto result = juce::Result::ok();

        if (! response.connected || response.status != 200 || ! (bool) response.body.getProperty ("ok", false))
            result = juce::Result::fail (failureText (response, "Signing out didn't work. Please try again."));

        deliver ([this, result, done]
        {
            if (result.wasOk())
                checkNow();   // back to this computer's own credits

            if (done != nullptr)
                done (result, {});
        });
    });
}

//==============================================================================
void FananServer::chat (const juce::var& request, Done done)
{
    auto body = request.isObject() ? request.clone() : object();
    body.getDynamicObject()->setProperty ("token", key);

    workers.addJob ([this, stillAlive = alive, body, done]
    {
        if (! stillAlive->load())
            return;

        const auto response = post ("stellaChat", body, chatTimeoutMs);
        const auto& answer = response.body;

        auto result = juce::Result::ok();
        bool lostConnection = false;

        if (! response.connected)
        {
            lostConnection = true;
            result = juce::Result::fail ("Not sent, and nothing was charged: the studio couldn't reach Stella AI. "
                                         "Send again once the connection is back.");
        }
        else if (response.status == 402 || answer.getProperty ("error", {}).toString() == "no_credits")
        {
            result = juce::Result::fail ("You're out of Stella AI credits. Use Buy credits to get more, then send again.");
        }
        else if (response.status != 200 || ! (bool) answer.getProperty ("ok", false))
        {
            result = juce::Result::fail (failureText (response, "Stella AI couldn't answer that. Please try again."));
        }

        deliver ([this, result, answer, done, lostConnection]
        {
            if (answer.hasProperty ("credits"))
            {
                const juce::ScopedLock lock (stateLock);
                credits = juce::jmax (0, (int) answer.getProperty ("credits", 0));
            }

            if (lostConnection)
                checkNow();   // the warning shows at once, and the retries begin
            else if (onStateChanged != nullptr)
                onStateChanged();

            if (done != nullptr)
                done (result, answer);
        });
    });
}

//==============================================================================
FananServer::Response FananServer::post (const juce::String& function, const juce::var& body, int timeoutMs)
{
    const auto url = juce::URL (settings.serverUrl + "/api/apps/" + settings.appId + "/functions/" + function)
                         .withPOSTData (juce::JSON::toString (body, true));

    juce::String headers;
    headers << "X-App-Id: " << settings.appId << "\r\n"
            << "Accept: application/json\r\n"
            << "Content-Type: application/json\r\n";

    const auto action = body.getProperty ("action", {}).toString();
    return fetch (url, headers, timeoutMs, true, function + (action.isNotEmpty() ? " " + action : juce::String()));
}

FananServer::Response FananServer::fetch (const juce::URL& url, const juce::String& headers, int timeoutMs, bool isPost,
                                          const juce::String& what)
{
    Response response;

    if (! alive->load())
        return response;

    const auto started = juce::Time::getMillisecondCounter();

    juce::WebInputStream stream (url, isPost);
    stream.withExtraHeaders (headers)
          .withConnectionTimeout (timeoutMs)
          .withNumRedirectsToFollow (5);

    {
        const juce::ScopedLock lock (streamsLock);
        activeStreams.push_back ({ &stream, started + (juce::uint32) (timeoutMs + overdueMarginMs), false });
    }

    if (alive->load() && stream.connect (nullptr))
    {
        response.status = stream.getStatusCode();
        response.connected = response.status > 0;

        if (response.connected)
            response.body = juce::JSON::parse (stream.readEntireStreamAsString());
    }

    bool brokenOff = false;

    {
        const juce::ScopedLock lock (streamsLock);

        const auto found = std::find_if (activeStreams.begin(), activeStreams.end(),
                                         [&stream] (const Waiting& w) { return w.stream == &stream; });

        if (found != activeStreams.end())
        {
            brokenOff = found->brokenOff;
            activeStreams.erase (found);
        }
    }

    if (alive->load())
    {
        const auto seconds = juce::String ((double) (juce::Time::getMillisecondCounter() - started) / 1000.0, 1) + " s";

        if (response.connected)
        {
            const auto ok = ! response.body.isObject() || (bool) response.body.getProperty ("ok", false);
            const auto message = response.body.getProperty ("message", response.body.getProperty ("error", {})).toString();

            writeLog (what + ": answered " + juce::String (response.status) + (ok ? "" : " (not ok)") + " in " + seconds
                      + (message.isNotEmpty() ? " - " + message.substring (0, 160) : juce::String()));
        }
        else
        {
            writeLog (what + ": no answer after " + seconds
                      + (brokenOff ? " (broken off: it hung past its time)"
                         : juce::Time::getMillisecondCounter() - started + 1000 >= (juce::uint32) timeoutMs ? " (timed out)"
                                                                                                             : " (couldn't connect)"));
        }
    }

    return response;
}

void FananServer::cancelOverdue()
{
    const auto now = juce::Time::getMillisecondCounter();
    const juce::ScopedLock lock (streamsLock);

    for (auto& waiting : activeStreams)
    {
        if (! waiting.brokenOff && (juce::int32) (now - waiting.deadline) > 0)
        {
            waiting.brokenOff = true;
            waiting.stream->cancel();
        }
    }
}

bool FananServer::canReachInternet()
{
    for (const auto& address : settings.internetTestUrls)
    {
        const auto response = fetch (juce::URL (address), {}, internetTimeoutMs, false, "internet test " + juce::URL (address).getDomain());

        if (response.connected && response.status >= 200 && response.status < 400)
            return true;
    }

    return false;
}

void FananServer::writeLog (const juce::String& line)
{
    static juce::CriticalSection logLock;
    const juce::ScopedLock lock (logLock);

    const auto file = getIdentityFile().getSiblingFile ("connection.log");
    file.getParentDirectory().createDirectory();

    // Kept short: the latest few thousand lines tell what happened.
    if (file.getSize() > logLimitBytes)
    {
        const auto text = file.loadFileAsString();
        file.replaceWithText (text.substring (text.length() / 2).fromFirstOccurrenceOf ("\n", false, false));
    }

    file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + line + "\n");
}

void FananServer::deliver (std::function<void()> fn)
{
    juce::MessageManager::callAsync ([stillAlive = alive, fn = std::move (fn)]
    {
        if (stillAlive->load())
            fn();
    });
}
