// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "SharedState.h"

namespace concert7
{
using namespace juce;

namespace Keys
{
    static const char* irFolder    = "irFolder";
    static const char* welcomeDone = "welcomeDone";
    static const char* holdDecay   = "holdDecay";      // before the groups: only DECAY could be kept
    static const char* keepGroups  = "keepOnPresetChange";
    static const char* uiScale     = "uiScale";
}

//==============================================================================
IRInstaller::IRInstaller() : Thread ("Concert7 IR installer") {}

IRInstaller::~IRInstaller()
{
    alive->store (false);
    stopThread (10000);
}

void IRInstaller::start (const File& zipFile, const File& dest)
{
    if (isThreadRunning()) return;
    {
        const ScopedLock sl (lock);
        zip = zipFile;
        destination = dest;
        errorDetail.clear();
    }
    progress = 0.0f;
    error = Error::None;
    phase = Phase::Checking;
    startThread();
}

void IRInstaller::cancel()
{
    signalThreadShouldExit();
}

String IRInstaller::getErrorDetail() const
{
    const ScopedLock sl (lock);
    return errorDetail;
}

File IRInstaller::getDestination() const
{
    const ScopedLock sl (lock);
    return destination;
}

void IRInstaller::run()
{
    File zipFile, dest;
    {
        const ScopedLock sl (lock);
        zipFile = zip;
        dest = destination;
    }

    auto finish = [this] (Error e, const String& detail) {
        {
            const ScopedLock sl (lock);
            errorDetail = detail;
        }
        error = e;
        phase = e == Error::None ? Phase::Done : Phase::Failed;
        const bool ok = e == Error::None;
        MessageManager::callAsync ([this, ok, flag = alive] { if (flag->load() && onFinished) onFinished (ok); });
    };

    // only accept the Samplicity Bricasti archive (or another archive full of its files)
    const int found = countBricastiEntries (zipFile);
    if (found < 0) { finish (Error::CannotOpen, zipFile.getFileName()); return; }
    if (found < 100) { finish (Error::NotBricasti, zipFile.getFileName()); return; }

    phase = Phase::Extracting;
    dest.createDirectory();
    String message;
    const bool ok = extractArchive (zipFile, dest, [this] (int done, int total) {
        progress = (float) done / (float) std::max (1, total);
        return ! threadShouldExit();
    }, message);

    if (threadShouldExit()) finish (Error::Cancelled, {});
    else finish (ok ? Error::None : Error::ExtractFailed, message);
}

int IRInstaller::countBricastiEntries (const File& zipFile)
{
    if (! zipFile.existsAsFile()) return -1;
    ZipFile archive (zipFile);
    if (archive.getNumEntries() == 0) return -1;
    int count = 0;
    for (int i = 0; i < archive.getNumEntries(); ++i)
        if (auto* e = archive.getEntry (i))
            if (IRLibrary::parseFileName (e->filename.replaceCharacter ('\\', '/')
                                              .fromLastOccurrenceOf ("/", false, false)
                                              .upToLastOccurrenceOf (".", false, false)).ok)
                ++count;
    return count;
}

bool IRInstaller::extractArchive (const File& zipFile, const File& dest,
                                  const std::function<bool (int, int)>& onProgress, String& errorMessage)
{
    ZipFile archive (zipFile);
    if (archive.getNumEntries() == 0)
    {
        errorMessage = "The archive could not be opened.";
        return false;
    }

    // the quad channel files contain everything (both sample rates, all 4 paths)
    auto isAudio = [] (const String& name) {
        return name.endsWithIgnoreCase (".wav") || name.endsWithIgnoreCase (".aif") || name.endsWithIgnoreCase (".aiff")
            || name.endsWithIgnoreCase (".flac");
    };
    std::vector<int> wanted;
    for (int i = 0; i < archive.getNumEntries(); ++i)
        if (auto* e = archive.getEntry (i); e != nullptr && e->filename.containsIgnoreCase ("quad channel files") && isAudio (e->filename))
            wanted.push_back (i);
    if (wanted.empty())
        for (int i = 0; i < archive.getNumEntries(); ++i)
            if (auto* e = archive.getEntry (i); e != nullptr && isAudio (e->filename))
                wanted.push_back (i);

    for (size_t n = 0; n < wanted.size(); ++n)
    {
        auto result = archive.uncompressEntry (wanted[n], dest, true);
        if (result.failed())
        {
            errorMessage = result.getErrorMessage();
            return false;
        }
        if (onProgress && ! onProgress ((int) n + 1, (int) wanted.size()))
            return false;
    }
    return true;
}

//==============================================================================
SharedState::SharedState()
{
    PropertiesFile::Options o;
    o.applicationName = "Concert7";
    o.filenameSuffix = "settings";
    o.folderName = "Concert7";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = PropertiesFile::storeAsXML;
    settings = settingsFileOverride() != File() ? std::make_unique<PropertiesFile> (settingsFileOverride(), o)
                                                 : std::make_unique<PropertiesFile> (o);

    auto folder = getIRFolder();
    if (! folder.isDirectory())
    {
        folder = autoDetectFolder();
        if (folder.isDirectory())
            settings->setValue (Keys::irFolder, folder.getFullPathName());
    }
    {
        const ScopedLock sl (libraryLock);
        library.scan (folder);
    }

    installer.onFinished = [this] (bool ok) {
        if (ok) setIRFolder (installer.getDestination());
    };
    userPresets = std::make_unique<UserPresetLibrary> (*settings);
    userPresets->rescan();
}

SharedState::~SharedState()
{
    installer.onFinished = nullptr;
    installer.cancel();
    settings->saveIfNeeded();
}

File& SharedState::settingsFileOverride()
{
    static File f;
    return f;
}

File& SharedState::installFolderOverride()
{
    static File f;
    return f;
}

File SharedState::getDefaultInstallFolder()
{
    if (installFolderOverride() != File())
        return installFolderOverride();

    auto base = File::getSpecialLocation (File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support"); // JUCE returns ~/Library on macOS
   #endif
    return base.getChildFile ("Concert7").getChildFile ("Impulse Responses");
}

File SharedState::autoDetectFolder()
{
    if (getDefaultInstallFolder().isDirectory())
        return getDefaultInstallFolder();

    for (auto base : { File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"),
                       File::getSpecialLocation (File::userMusicDirectory),
                       File::getSpecialLocation (File::userDocumentsDirectory) })
    {
        if (! base.isDirectory()) continue;
        File variantFolder;
        for (const auto& entry : RangedDirectoryIterator (base, false, "*", File::findDirectories))
        {
            const auto f = entry.getFile();
            if (! f.getFileName().startsWithIgnoreCase ("Samplicity - Bricasti")) continue;
            // Finder wraps the archive in one folder; "unzip" puts the variant folders side by side
            if (! f.getFileName().containsIgnoreCase ("files"))
                return f;
            if (variantFolder == File() || f.getFileName().containsIgnoreCase ("quad"))
                variantFolder = f;
        }
        if (variantFolder.isDirectory())
            return variantFolder;
    }
    return {};
}

File SharedState::getIRFolder() const
{
    const auto path = settings->getValue (Keys::irFolder);
    return path.isEmpty() ? File() : File (path);
}

void SharedState::setIRFolder (const File& folder)
{
    settings->setValue (Keys::irFolder, folder.getFullPathName());
    settings->saveIfNeeded();
    rescan();
}

void SharedState::rescan()
{
    {
        const ScopedLock sl (libraryLock);
        library.scan (getIRFolder());
    }
    sendChangeMessage();
}

bool SharedState::getPreset (const String& id, IRPreset& out) const
{
    const ScopedLock sl (libraryLock);
    if (auto* p = library.find (id)) { out = *p; return true; }
    return false;
}

bool SharedState::getPresetAt (int index, IRPreset& out) const
{
    const ScopedLock sl (libraryLock);
    if (! isPositiveAndBelow (index, (int) library.getPresets().size())) return false;
    out = library.getPresets()[(size_t) index];
    return true;
}

int SharedState::getNumPresets() const
{
    const ScopedLock sl (libraryLock);
    return (int) library.getPresets().size();
}

int SharedState::indexOfPreset (const String& id) const
{
    const ScopedLock sl (libraryLock);
    return library.indexOf (id);
}

String SharedState::getPresetIdForErPattern (int pattern) const
{
    const ScopedLock sl (libraryLock);
    const auto& list = library.getErPatternPresets();
    if (! isPositiveAndNotGreaterThan (pattern, (int) list.size()) || pattern == 0) return {};
    return library.getPresets()[(size_t) list[(size_t) pattern - 1]].id;
}

String SharedState::getDefaultPresetId() const
{
    const ScopedLock sl (libraryLock);
    return library.getPresets().empty() ? String() : library.getPresets().front().id;
}

void SharedState::withLibrary (const std::function<void (const IRLibrary&)>& f) const
{
    const ScopedLock sl (libraryLock);
    f (library);
}

bool SharedState::isWelcomeDone() const   { return settings->getBoolValue (Keys::welcomeDone, false); }
void SharedState::setWelcomeDone (bool d) { settings->setValue (Keys::welcomeDone, d); settings->saveIfNeeded(); }
GroupMask SharedState::getKeptGroups() const
{
    if (! settings->containsKey (Keys::keepGroups))
        return settings->getBoolValue (Keys::holdDecay, false) ? groupBit (ParamGroup::Decay) : 0;
    GroupMask mask = 0;
    for (auto& key : StringArray::fromTokens (settings->getValue (Keys::keepGroups), ",", {}))
        for (int g = 0; g < kNumParamGroups; ++g)
            if (key.trim() == groupKey ((ParamGroup) g))
                mask |= groupBit ((ParamGroup) g);
    return mask;
}

void SharedState::setKeptGroups (GroupMask groups)
{
    StringArray keys;
    for (int g = 0; g < kNumParamGroups; ++g)
        if ((groups & groupBit ((ParamGroup) g)) != 0)
            keys.add (groupKey ((ParamGroup) g));
    settings->setValue (Keys::keepGroups, keys.joinIntoString (","));
    settings->removeValue (Keys::holdDecay);
    settings->saveIfNeeded();
}
float SharedState::getUIScale() const     { return (float) settings->getDoubleValue (Keys::uiScale, 1.0); }
void SharedState::setUIScale (float s)    { settings->setValue (Keys::uiScale, s); settings->saveIfNeeded(); }
} // namespace concert7
