// C:\workspace\Stella AI Studio\src\Project.h

#pragma once

#include <juce_core/juce_core.h>

#include <functional>

//==============================================================================
enum class PluginKind
{
    instrument,   // MIDI in, audio out
    effect,       // audio in, audio out
    noteEffect    // MIDI in, MIDI out
};

//==============================================================================
/** What a plugin is called and who makes it. The ids are fixed when the project is
    created and never change, so hosts keep recognising the plugin across versions. */
struct ProjectInfo
{
    juce::String name;
    juce::String vendor;
    PluginKind kind = PluginKind::instrument;
    juce::String version { "1.0.0" };
    juce::String pluginId;   // reverse-DNS, e.g. com.mycompany.myplugin (the CLAP id)
    juce::String uuid;       // the VST3 class id is derived from this
    juce::String created;    // ISO 8601
};

//==============================================================================
/**
    A plugin being built: one folder on disk.

        <folder>/project.stella   what the plugin is (JSON)
        <folder>/modules/         its DSP modules, written by the AI
        <folder>/gui/             its GUI layout and widget styles
        <folder>/assets/          images and other files the plugin carries

    There is no Save: the AI writes straight into the folder and the compiler reads
    from it, so the project on disk is always the current one. Changes to the
    project's info are written at once.
*/
class Project final
{
public:
    static constexpr const char* fileName = "project.stella";
    static constexpr int formatVersion = 1;

    Project() = default;

    /** Makes <parentFolder>/<name> (numbered if taken) with its subfolders and file. */
    juce::Result create (const juce::File& parentFolder, ProjectInfo newInfo);

    /** Accepts the project file or its folder. */
    juce::Result open (const juce::File& projectFileOrFolder);

    void close();

    bool isOpen() const noexcept                    { return opened; }
    const ProjectInfo& getInfo() const noexcept     { return info; }

    /** Writes the new info at once. The ids and creation time can't be changed. */
    juce::Result updateInfo (const juce::String& newName, const juce::String& newVendor,
                             PluginKind newKind, const juce::String& newVersion);

    juce::File getFolder() const                    { return folder; }
    juce::File getProjectFile() const               { return folder.getChildFile (fileName); }
    juce::File getModulesFolder() const             { return folder.getChildFile ("modules"); }
    juce::File getGuiFolder() const                 { return folder.getChildFile ("gui"); }
    juce::File getAssetsFolder() const              { return folder.getChildFile ("assets"); }

    /** Called on the message thread whenever a project opens, closes or changes. */
    std::function<void()> onChanged;

    //==============================================================================
    static juce::String kindToString (PluginKind kind);
    static PluginKind kindFromString (const juce::String& text);
    static juce::String kindDisplayName (PluginKind kind);

    /** e.g. "My Company" + "Big Reverb" -> "com.mycompany.bigreverb" */
    static juce::String makePluginId (const juce::String& vendor, const juce::String& name);

private:
    juce::Result write() const;
    juce::Result makeSubfolders() const;
    void notify();

    juce::File folder;
    ProjectInfo info;
    bool opened = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Project)
};
