// C:\workspace\Stella AI Studio\src\CloudProjects.cpp

#include "CloudProjects.h"

namespace
{
    juce::var object()
    {
        return juce::var (new juce::DynamicObject());
    }

    /** The project files that go to the cloud, relative to the project folder. */
    juce::Array<juce::File> projectFiles (const juce::File& folder)
    {
        juce::Array<juce::File> files;

        for (const auto* name : { "project.stella", "graph.json" })
            if (folder.getChildFile (name).existsAsFile())
                files.add (folder.getChildFile (name));

        auto modules = folder.getChildFile ("modules").findChildFiles (juce::File::findFiles, false, "*.cpp;*.h");
        modules.sort();
        files.addArray (modules);

        for (const auto* name : { "layout.json", "schematic.json" })
            if (folder.getChildFile ("gui").getChildFile (name).existsAsFile())
                files.add (folder.getChildFile ("gui").getChildFile (name));

        return files;
    }

    juce::String friendlyTime (const juce::Time& time)
    {
        const auto seconds = (juce::Time::getCurrentTime() - time).inSeconds();

        if (seconds < 60)   return "just now";
        if (seconds < 3600) return juce::String ((int) (seconds / 60)) + " min ago";
        return time.toString (true, true, false, true);
    }
}

//==============================================================================
CloudProjects::CloudProjects (FananServer& s, Project& p)
    : server (s), project (p)
{
    startTimer (5000);
}

CloudProjects::~CloudProjects()
{
    *alive = false;
    stopTimer();
}

void CloudProjects::setState (State newState, const juce::String& newDetail)
{
    state = newState;
    detail = newDetail;

    if (onStatusChanged != nullptr)
        onStatusChanged();
}

juce::String CloudProjects::getStatusText() const
{
    if (! project.isOpen())
        return "Cloud: no project open";

    if (! server.isSignedIn())
        return "Cloud: sign in to keep this project in the cloud";

    switch (state)
    {
        case State::off:       return "Cloud: not saved yet";
        case State::waiting:   return juce::String::fromUTF8 ("Cloud: saving soon\xe2\x80\xa6");
        case State::saving:    return juce::String::fromUTF8 ("Cloud: saving\xe2\x80\xa6");
        case State::saved:     return "Cloud: saved " + detail;
        case State::failed:    return "Cloud: not saved (" + detail + ")";
        case State::conflict:  return "Cloud: changed on another computer";
    }

    return {};
}

//==============================================================================
juce::var CloudProjects::readState (const juce::File& folder) const
{
    return juce::JSON::parse (folder.getChildFile (stateFileName));
}

void CloudProjects::writeState (const juce::File& folder, int version, const juce::String& hash, const juce::String& saved) const
{
    auto json = object();
    json.getDynamicObject()->setProperty ("version", version);
    json.getDynamicObject()->setProperty ("hash", hash);
    json.getDynamicObject()->setProperty ("saved", saved);
    folder.getChildFile (stateFileName).replaceWithText (juce::JSON::toString (json));
}

juce::String CloudProjects::hashOf (const juce::var& files)
{
    juce::String all;

    if (auto* o = files.getDynamicObject())
        for (const auto& entry : o->getProperties())
            all << entry.name.toString() << '\n' << entry.value.toString() << '\n';

    return juce::String::toHexString ((juce::int64) all.hashCode64());
}

bool CloudProjects::collect (const juce::File& folder, juce::var& files, juce::String& hash, juce::String& error)
{
    files = object();
    size_t total = 0;

    for (const auto& file : projectFiles (folder))
    {
        const auto text = file.loadFileAsString();
        total += text.getNumBytesAsUTF8();

        if (total > (size_t) maxBytes)
        {
            error = "the project is over 2 MB";
            return false;
        }

        files.getDynamicObject()->setProperty (file.getRelativePathFrom (folder).replaceCharacter ('\\', '/'), text);
    }

    hash = hashOf (files);
    return true;
}

//==============================================================================
void CloudProjects::projectChanged()
{
    pendingHash.clear();
    trackedUuid = project.isOpen() ? project.getInfo().uuid : juce::String();

    if (! project.isOpen())
    {
        setState (State::off);
        return;
    }

    const auto saved = readState (project.getFolder());
    const auto when = juce::Time::fromISO8601 (saved.getProperty ("saved", {}).toString());

    setState ((int) saved.getProperty ("version", 0) > 0 ? State::saved : State::off,
              (int) saved.getProperty ("version", 0) > 0 ? friendlyTime (when) : juce::String());
}

void CloudProjects::timerCallback()
{
    if (busy || ! project.isOpen() || ! server.isSignedIn() || ! server.isReady() || state == State::conflict)
        return;

    juce::var files;
    juce::String hash, error;

    if (! collect (project.getFolder(), files, hash, error))
    {
        if (state != State::failed)
            setState (State::failed, error);

        return;
    }

    const auto saved = readState (project.getFolder());

    if (hash == saved.getProperty ("hash", {}).toString())
    {
        pendingHash.clear();

        // Keep "saved 3 min ago" fresh.
        if (state == State::saved || state == State::off)
            setState (State::saved, friendlyTime (juce::Time::fromISO8601 (saved.getProperty ("saved", {}).toString())));

        return;
    }

    // Changed: save once it has stayed the same for one tick, so a burst of edits is one save.
    if (hash != pendingHash)
    {
        pendingHash = hash;
        setState (State::waiting);
        return;
    }

    saveNow();
}

void CloudProjects::saveNow (bool force)
{
    if (busy || ! project.isOpen())
        return;

    juce::var files;
    juce::String hash, error;

    if (! collect (project.getFolder(), files, hash, error))
    {
        setState (State::failed, error);
        return;
    }

    const auto& info = project.getInfo();
    const auto folder = project.getFolder();
    const auto uuid = info.uuid;

    auto details = object();
    details.getDynamicObject()->setProperty ("uuid", info.uuid);
    details.getDynamicObject()->setProperty ("name", info.name);
    details.getDynamicObject()->setProperty ("kind", Project::kindToString (info.kind));
    details.getDynamicObject()->setProperty ("vendor", info.vendor);

    auto request = object();
    request.getDynamicObject()->setProperty ("action", "save");
    request.getDynamicObject()->setProperty ("project", details);
    request.getDynamicObject()->setProperty ("files", files);
    request.getDynamicObject()->setProperty ("base_version", (int) readState (folder).getProperty ("version", 0));
    request.getDynamicObject()->setProperty ("force", force);

    busy = true;
    setState (State::saving);

    server.cloud (request, [this, stillAlive = alive, folder, hash, uuid] (const juce::Result& result, const juce::var& answer)
    {
        if (! *stillAlive)
            return;

        busy = false;

        // The answer may belong to a project that's been closed since.
        if (! project.isOpen() || project.getInfo().uuid != uuid)
            return;

        if (result.getErrorMessage() == "conflict")
        {
            setState (State::conflict);

            if (onConflict != nullptr)
                onConflict (answer.getProperty ("updated", {}).toString());

            return;
        }

        if (result.failed())
        {
            setState (State::failed, result.getErrorMessage());
            return;
        }

        const auto now = juce::Time::getCurrentTime();
        writeState (folder, (int) answer.getProperty ("version", 1), hash, now.toISO8601 (true));
        pendingHash.clear();
        setState (State::saved, friendlyTime (now));
    });
}

//==============================================================================
void CloudProjects::list (std::function<void (const juce::Result&, const juce::Array<Entry>&)> done)
{
    auto request = object();
    request.getDynamicObject()->setProperty ("action", "list");

    server.cloud (request, [stillAlive = alive, done] (const juce::Result& result, const juce::var& answer)
    {
        if (! *stillAlive || done == nullptr)
            return;

        juce::Array<Entry> entries;

        if (const auto* projects = answer.getProperty ("projects", {}).getArray())
        {
            for (const auto& p : *projects)
            {
                Entry entry;
                entry.uuid = p.getProperty ("uuid", {}).toString();
                entry.name = p.getProperty ("name", {}).toString();
                entry.kind = p.getProperty ("kind", {}).toString();
                entry.updated = p.getProperty ("updated", {}).toString();
                entry.version = (int) p.getProperty ("version", 0);
                entry.size = (int) p.getProperty ("size", 0);

                if (entry.uuid.isNotEmpty())
                    entries.add (entry);
            }
        }

        done (result, entries);
    });
}

void CloudProjects::download (const juce::String& uuid, const juce::File& folder, std::function<void (const juce::Result&)> done)
{
    auto request = object();
    request.getDynamicObject()->setProperty ("action", "load");
    request.getDynamicObject()->setProperty ("uuid", uuid);

    server.cloud (request, [this, stillAlive = alive, folder, done] (const juce::Result& result, const juce::var& answer)
    {
        if (! *stillAlive)
            return;

        auto finish = [&done] (const juce::Result& r) { if (done != nullptr) done (r); };

        if (result.failed())
            return finish (result);

        const auto files = answer.getProperty ("files", {});

        if (files.getDynamicObject() == nullptr || ! files.hasProperty ("project.stella"))
            return finish (juce::Result::fail ("The cloud copy is incomplete."));

        // The cloud copy replaces the files it covers; modules it no longer has go.
        if (! folder.isDirectory() && ! folder.createDirectory())
            return finish (juce::Result::fail ("Couldn't create " + folder.getFullPathName()));

        for (const auto& old : folder.getChildFile ("modules").findChildFiles (juce::File::findFiles, false, "*.cpp;*.h"))
            old.deleteFile();

        for (const auto& entry : files.getDynamicObject()->getProperties())
        {
            const auto path = entry.name.toString();

            // Only plain relative paths inside the project.
            if (path.contains ("..") || path.startsWithChar ('/') || path.containsChar (':') || path.containsChar ('\\'))
                continue;

            const auto target = folder.getChildFile (path);
            target.getParentDirectory().createDirectory();

            if (! target.replaceWithText (entry.value.toString()))
                return finish (juce::Result::fail ("Couldn't write " + target.getFullPathName()));
        }

        for (const auto* sub : { "modules", "gui", "assets" })
            folder.getChildFile (sub).createDirectory();

        // Remember it as saved, hashed the same way a local save is, so it doesn't go straight back up.
        juce::var local;
        juce::String hash, error;
        collect (folder, local, hash, error);
        writeState (folder, (int) answer.getProperty ("version", 1), hash, juce::Time::getCurrentTime().toISO8601 (true));

        if (project.isOpen() && project.getFolder() == folder)
            pendingHash.clear();

        finish (juce::Result::ok());
    });
}
