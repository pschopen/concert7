// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "dsp/IRLibrary.h"
#include "Parameters.h"
#include "UserPresets.h"
#include <atomic>
#include <functional>
#include <memory>

namespace concert7
{
/**
    Installs an IR archive that the user downloaded and extracts it. An M7 library of
    combined responses gives its quad channel files, one of split captures all of them;
    any other archive (when accepted) all of its audio files, for OTHER. A 7z is unpacked
    by the system's tar (libarchive). Runs on its own thread; the setup dialog polls the state.
*/
class IRInstaller : private juce::Thread
{
public:
    enum class Phase { Idle, Checking, Extracting, Done, Failed };
    enum class Error { None, CannotOpen, NotALibrary, ExtractFailed, Cancelled, NoUnpacker };
    enum class Kind { Unknown, Combined, Split, Other };

    IRInstaller();
    ~IRInstaller() override;

    /** Extracts into combinedFolder, splitFolder or (other impulse responses, if accepted) a subfolder of ownFolder
        named after the archive, depending on what the archive holds. */
    void start (const juce::File& archive, const juce::File& combinedFolder, const juce::File& splitFolder,
                const juce::File& ownFolder, bool acceptOther);
    void cancel();
    bool isBusy() const { return isThreadRunning(); }

    Phase getPhase() const { return phase.load(); }
    Error getError() const { return error.load(); }
    float getProgress() const { return progress.load(); }   // 0..1 while extracting
    juce::String getErrorDetail() const;
    juce::File getDestination() const;
    Kind getKind() const { return kind.load(); }

    std::function<void (bool success)> onFinished; // called on the message thread

    /** Number of M7 IR files named by bank and number in an archive, -1 if it cannot be opened. */
    static int countBankEntries (const juce::File& zip);
    /** The entries of an archive (ZIP, or anything the system's tar lists); false if it cannot be read. */
    static bool listArchive (const juce::File& archive, juce::StringArray& names);
    /** Which library an archive holds (at least 100 of its files; Other: any audio files). */
    static Kind detect (const juce::StringArray& names);

    /** Extracts the quad channel files of a combined library (all audio files if there are none), all captures of
        split ones, all audio files of any other archive. onProgress (done, total) may return false to cancel. */
    static bool extractArchive (const juce::File& archive, const juce::File& dest,
                                const std::function<bool (int, int)>& onProgress, juce::String& error);

private:
    void run() override;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<Phase> phase { Phase::Idle };
    std::atomic<Error> error { Error::None };
    std::atomic<float> progress { 0.0f };
    std::atomic<Kind> kind { Kind::Unknown };
    mutable juce::CriticalSection lock;
    juce::String errorDetail;
    juce::File zip, destination, combinedDestination, splitDestination, ownDestination;
    bool acceptOther = false;
};

/**
    Process wide state shared by all plugin instances: global settings, the
    scanned IR library and the archive installer.
*/
class SharedState : public juce::ChangeBroadcaster
{
public:
    SharedState();
    ~SharedState() override;

    juce::PropertiesFile& getSettings() { return *settings; }

    // ---- IR library
    juce::File getIRFolder() const;
    /** Sets and scans a new folder (message thread). */
    void setIRFolder (const juce::File& folder);
    /** The folder of the split captures (they are found in the other folders as well). */
    juce::File getSplitFolder() const;
    void setSplitFolder (const juce::File& folder);
    /** The folder of other impulse responses (OTHER), categories by subfolder. */
    juce::File getOwnFolder() const;
    void setOwnFolder (const juce::File& folder);
    /** A folder the user chose: the own folder when asked for (OTHER), else by what it holds: the split folder for
        split captures alone, the IR folder for an M7 library, the own folder for anything else. */
    void useFolder (const juce::File& folder, bool asOwn = false);
    void rescan();

    /** Copy of a preset (thread safe). */
    bool getPreset (const juce::String& id, IRPreset& out) const;
    bool getPresetAt (int index, IRPreset& out) const;
    int getNumPresets() const;
    int indexOfPreset (const juce::String& id) const;
    juce::String getPresetIdForErPattern (int pattern, IROrigin origin = IROrigin::Combined) const; // 1..32
    /** The first preset of that library (of any, when it is not installed). */
    juce::String getDefaultPresetId (IROrigin origin = IROrigin::Combined) const;
    /** The same preset in the other M7 library, empty when there is none. */
    juce::String getCounterpart (const juce::String& id, IROrigin target) const;
    bool hasLibrary() const { return getNumPresets() > 0; }
    bool hasLibrary (IROrigin origin) const;
    /** The library a new instance starts with: the one chosen last. */
    IROrigin getPreferredOrigin() const;
    void setPreferredOrigin (IROrigin origin);

    /** Runs f with the library locked (keep it short). */
    void withLibrary (const std::function<void (const IRLibrary&)>& f) const;

    // ---- global options
    bool isWelcomeDone() const;
    void setWelcomeDone (bool done);
    /** Groups of parameters that stay when another preset (or a user preset) is chosen. */
    GroupMask getKeptGroups() const;
    void setKeptGroups (GroupMask groups);
    bool isKept (ParamGroup g) const { return (getKeptGroups() & groupBit (g)) != 0; }
    float getUIScale() const;
    void setUIScale (float scale);

    IRInstaller& getInstaller() { return installer; }
    /** User presets, favourites and tags (message thread). */
    UserPresetLibrary& getUserPresets() { return *userPresets; }
    /** Where archives dropped into the plugin are extracted to. */
    static juce::File getDefaultInstallFolder();
    static juce::File getDefaultSplitInstallFolder();
    static juce::File getDefaultOwnInstallFolder();

    /** Looks for an already extracted M7 library (of that kind) in typical places. */
    static juce::File autoDetectFolder (IROrigin kind = IROrigin::Combined);

    /** Lets tests use their own settings file instead of the user's. Call before the first instance exists. */
    static juce::File& settingsFileOverride();
    /** Lets tests install archives somewhere else than the user's library folder. */
    static juce::File& installFolderOverride();
    /** Lets tests skip the search for libraries in Downloads, Music and Documents. */
    static bool& searchTypicalPlaces();

private:
    std::unique_ptr<juce::PropertiesFile> settings;
    mutable juce::CriticalSection libraryLock;
    IRLibrary library;
    IRInstaller installer;
    std::unique_ptr<UserPresetLibrary> userPresets;
};
} // namespace concert7
