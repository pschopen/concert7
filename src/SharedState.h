// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "dsp/IRLibrary.h"
#include <atomic>
#include <functional>
#include <memory>

namespace concert7
{
/**
    Installs the Samplicity Bricasti IR archive that the user downloaded: checks
    that the ZIP really is that library and extracts the quad channel files into
    the IR folder. Runs on its own thread; the setup dialog polls the state.
*/
class IRInstaller : private juce::Thread
{
public:
    enum class Phase { Idle, Checking, Extracting, Done, Failed };
    enum class Error { None, CannotOpen, NotBricasti, ExtractFailed, Cancelled };

    IRInstaller();
    ~IRInstaller() override;

    void start (const juce::File& zipFile, const juce::File& destinationFolder);
    void cancel();
    bool isBusy() const { return isThreadRunning(); }

    Phase getPhase() const { return phase.load(); }
    Error getError() const { return error.load(); }
    float getProgress() const { return progress.load(); }   // 0..1 while extracting
    juce::String getErrorDetail() const;
    juce::File getDestination() const;

    std::function<void (bool success)> onFinished; // called on the message thread

    /** Number of Bricasti IR files (by name) in an archive, -1 if it cannot be opened. */
    static int countBricastiEntries (const juce::File& zip);

    /** Extracts the quad channel files of the Samplicity archive (all audio files if there are none).
        onProgress (done, total) may return false to cancel. */
    static bool extractArchive (const juce::File& zip, const juce::File& dest,
                                const std::function<bool (int, int)>& onProgress, juce::String& error);

private:
    void run() override;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<Phase> phase { Phase::Idle };
    std::atomic<Error> error { Error::None };
    std::atomic<float> progress { 0.0f };
    mutable juce::CriticalSection lock;
    juce::String errorDetail;
    juce::File zip, destination;
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
    void rescan();

    /** Copy of a preset (thread safe). */
    bool getPreset (const juce::String& id, IRPreset& out) const;
    bool getPresetAt (int index, IRPreset& out) const;
    int getNumPresets() const;
    int indexOfPreset (const juce::String& id) const;
    juce::String getPresetIdForErPattern (int pattern) const; // 1..32
    juce::String getDefaultPresetId() const;
    bool hasLibrary() const { return getNumPresets() > 0; }

    /** Runs f with the library locked (keep it short). */
    void withLibrary (const std::function<void (const IRLibrary&)>& f) const;

    // ---- global options
    bool isWelcomeDone() const;
    void setWelcomeDone (bool done);
    bool getHoldDecay() const;
    void setHoldDecay (bool hold);
    float getUIScale() const;
    void setUIScale (float scale);

    IRInstaller& getInstaller() { return installer; }
    /** Where archives dropped into the plugin are extracted to. */
    static juce::File getDefaultInstallFolder();

    /** Looks for an already extracted Bricasti library in typical places. */
    static juce::File autoDetectFolder();

    /** Lets tests use their own settings file instead of the user's. Call before the first instance exists. */
    static juce::File& settingsFileOverride();
    /** Lets tests install archives somewhere else than the user's library folder. */
    static juce::File& installFolderOverride();

private:
    std::unique_ptr<juce::PropertiesFile> settings;
    mutable juce::CriticalSection libraryLock;
    IRLibrary library;
    IRInstaller installer;
};
} // namespace concert7
