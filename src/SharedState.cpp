// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "SharedState.h"

namespace concert7
{
using namespace juce;

namespace Keys
{
    static const char* irFolder    = "irFolder";
    static const char* splitFolder = "splitFolder";
    static const char* ownFolder   = "ownFolder";
    static const char* library     = "library";        // the library new instances start with
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

void IRInstaller::start (const File& zipFile, const File& combinedDest, const File& splitDest, const File& ownDest,
                         bool acceptOtherArchives)
{
    if (isThreadRunning()) return;
    {
        const ScopedLock sl (lock);
        zip = zipFile;
        combinedDestination = combinedDest;
        splitDestination = splitDest;
        ownDestination = ownDest;
        acceptOther = acceptOtherArchives;
        destination = combinedDest;
        errorDetail.clear();
    }
    kind = Kind::Unknown;
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
    File zipFile, combinedDest, splitDest, ownDest;
    bool other = false;
    {
        const ScopedLock sl (lock);
        zipFile = zip;
        combinedDest = combinedDestination;
        splitDest = splitDestination;
        ownDest = ownDestination;
        other = acceptOther;
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

    // an M7 library (or another archive full of its files); any impulse responses only where asked for (OTHER)
    StringArray names;
    if (! listArchive (zipFile, names))
    {
        const bool sevenZip = zipFile.hasFileExtension ("7z");
        finish (sevenZip && zipFile.existsAsFile() ? Error::NoUnpacker : Error::CannotOpen, zipFile.getFileName());
        return;
    }
    kind = detect (names);
    if (kind == Kind::Unknown || (kind == Kind::Other && ! other)) { finish (Error::NotALibrary, zipFile.getFileName()); return; }
    const auto dest = kind == Kind::Split ? splitDest
                    : kind == Kind::Other ? ownDest.getChildFile (File::createLegalFileName (zipFile.getFileNameWithoutExtension()))
                    : combinedDest;
    {
        const ScopedLock sl (lock);
        destination = dest;
    }

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

namespace
{
    String baseName (const String& path)
    {
        return path.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
    }

    bool isAudio (const String& name)
    {
        return name.endsWithIgnoreCase (".wav") || name.endsWithIgnoreCase (".aif") || name.endsWithIgnoreCase (".aiff")
            || name.endsWithIgnoreCase (".flac");
    }

    /** The system's tar where it reads 7z (libarchive: macOS, Windows 10 and later, bsdtar on Linux). */
    StringArray tarCandidates()
    {
       #if JUCE_MAC
        return { "/usr/bin/tar", "/usr/bin/bsdtar" };
       #elif JUCE_WINDOWS
        return { File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("tar.exe").getFullPathName(), "tar" };
       #else
        return { "bsdtar", "tar" };
       #endif
    }
}

int IRInstaller::countBankEntries (const File& zipFile)
{
    if (! zipFile.existsAsFile()) return -1;
    ZipFile archive (zipFile);
    if (archive.getNumEntries() == 0) return -1;
    int count = 0;
    for (int i = 0; i < archive.getNumEntries(); ++i)
        if (auto* e = archive.getEntry (i))
            if (IRLibrary::parseFileName (baseName (e->filename)).ok)
                ++count;
    return count;
}

bool IRInstaller::listArchive (const File& archive, StringArray& names)
{
    names.clear();
    if (! archive.existsAsFile()) return false;
    if (! archive.hasFileExtension ("7z"))
    {
        ZipFile z (archive);
        for (int i = 0; i < z.getNumEntries(); ++i)
            if (auto* e = z.getEntry (i))
                names.add (e->filename.replaceCharacter ('\\', '/'));
        if (! names.isEmpty() || ! archive.hasFileExtension ("zip"))
            return ! names.isEmpty();
    }
    for (auto& tar : tarCandidates())
    {
        ChildProcess p;
        if (! p.start (StringArray { tar, "-tf", archive.getFullPathName() }, ChildProcess::wantStdOut)) continue;
        const auto out = p.readAllProcessOutput();
        if (p.getExitCode() != 0) continue;
        names = StringArray::fromLines (out);
        names.removeEmptyStrings();
        if (! names.isEmpty()) return true;
    }
    return false;
}

IRInstaller::Kind IRInstaller::detect (const StringArray& names)
{
    int combined = 0, split = 0, audio = 0;
    for (auto& n : names)
    {
        if (IRLibrary::parseFileName (baseName (n)).ok) ++combined;
        else if (IRLibrary::parseSplitPath (n).ok) ++split;
        if (isAudio (n)) ++audio;
    }
    if (split >= 100 && split >= combined) return Kind::Split;
    if (combined >= 100) return Kind::Combined;
    return audio > 0 ? Kind::Other : Kind::Unknown;
}

bool IRInstaller::extractArchive (const File& zipFile, const File& dest,
                                  const std::function<bool (int, int)>& onProgress, String& errorMessage)
{
    StringArray names;
    if (! listArchive (zipFile, names))
    {
        errorMessage = "The archive could not be opened.";
        return false;
    }
    const bool split = detect (names) == Kind::Split;

    if (zipFile.hasFileExtension ("7z"))
    {
        // the system's tar unpacks it (into dest, with the archive's own folder); its listing counts the progress
        int total = 0;
        for (auto& n : names)
            if (isAudio (n)) ++total;
        for (auto& tar : tarCandidates())
        {
            ChildProcess p;
            if (! p.start (StringArray { tar, "-xvf", zipFile.getFullPathName(), "-C", dest.getFullPathName() },
                           ChildProcess::wantStdOut | ChildProcess::wantStdErr))
                continue;
            int done = 0;
            String pending;
            char buffer[4096];
            for (;;)
            {
                const int n = p.readProcessOutput (buffer, (int) sizeof (buffer));
                if (n <= 0)
                {
                    if (! p.isRunning()) break;
                    Thread::sleep (20);
                    continue;
                }
                pending += String::fromUTF8 (buffer, n);
                auto lines = StringArray::fromLines (pending);
                pending = pending.endsWithChar ('\n') ? String() : lines[lines.size() - 1];
                for (int i = 0; i < lines.size() - (pending.isEmpty() ? 0 : 1); ++i)
                    if (isAudio (lines[i].trim())) ++done;
                if (onProgress && ! onProgress (std::min (done, total), total))
                {
                    p.kill();
                    return false;
                }
            }
            if (p.getExitCode() == 0)
            {
                if (onProgress) onProgress (total, total);
                return true;
            }
            errorMessage = "tar could not unpack the archive.";
            return false;
        }
        errorMessage = "No program to unpack 7z archives was found.";
        return false;
    }

    ZipFile archive (zipFile);
    if (archive.getNumEntries() == 0)
    {
        errorMessage = "The archive could not be opened.";
        return false;
    }

    // a combined library: the quad channel files contain everything (both sample rates, all 4 paths).
    // Split captures: all of them (both blocks of every take). Anything else: all audio files.
    std::vector<int> wanted;
    for (int i = 0; i < archive.getNumEntries(); ++i)
        if (auto* e = archive.getEntry (i); e != nullptr && isAudio (e->filename))
        {
            const auto name = e->filename.replaceCharacter ('\\', '/');
            if (split ? IRLibrary::parseSplitPath (name).ok : name.containsIgnoreCase ("quad channel files"))
                wanted.push_back (i);
        }
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
        folder = autoDetectFolder (IROrigin::Combined);
        if (folder.isDirectory())
            settings->setValue (Keys::irFolder, folder.getFullPathName());
    }
    auto splitFolder = getSplitFolder();
    if (! splitFolder.isDirectory())
    {
        splitFolder = autoDetectFolder (IROrigin::Split);
        if (splitFolder.isDirectory())
            settings->setValue (Keys::splitFolder, splitFolder.getFullPathName());
    }
    {
        const ScopedLock sl (libraryLock);
        library.scan (folder, splitFolder, getOwnFolder());
    }

    installer.onFinished = [this] (bool ok) {
        if (! ok) return;
        const auto kind = installer.getKind();
        if (kind == IRInstaller::Kind::Split) setSplitFolder (installer.getDestination());
        else if (kind == IRInstaller::Kind::Other) setOwnFolder (installer.getDestination().getParentDirectory());
        else setIRFolder (installer.getDestination());
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

bool& SharedState::searchTypicalPlaces()
{
    static bool search = true;
    return search;
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

File SharedState::getDefaultSplitInstallFolder()
{
    return getDefaultInstallFolder().getSiblingFile ("Split Captures");
}

File SharedState::getDefaultOwnInstallFolder()
{
    return getDefaultInstallFolder().getSiblingFile ("Other Impulse Responses");
}

File SharedState::autoDetectFolder (IROrigin kind)
{
    const auto installed = kind == IROrigin::Split ? getDefaultSplitInstallFolder() : getDefaultInstallFolder();
    if (installed.isDirectory())
        return installed;
    if (! searchTypicalPlaces())
        return {};

    // an unpacked M7 library in Downloads, Music or Documents: a folder named after the hardware that holds presets
    // of that kind. Of a library unpacked into several folders (one per file variant) the quad files are preferred.
    for (auto base : { File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"),
                       File::getSpecialLocation (File::userMusicDirectory),
                       File::getSpecialLocation (File::userDocumentsDirectory) })
    {
        if (! base.isDirectory()) continue;
        File found;
        for (const auto& entry : RangedDirectoryIterator (base, false, "*", File::findDirectories))
        {
            const auto f = entry.getFile();
            if (! f.getFileName().containsIgnoreCase ("Bricasti")) continue;
            IRLibrary probe;
            probe.scan (f);
            if (! probe.has (kind) || (kind == IROrigin::Combined && probe.has (IROrigin::Split) && ! probe.has (IROrigin::Combined)))
                continue;
            if (found == File() || f.getFileName().containsIgnoreCase ("quad"))
                found = f;
        }
        if (found.isDirectory())
            return found;
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

File SharedState::getSplitFolder() const
{
    const auto path = settings->getValue (Keys::splitFolder);
    return path.isEmpty() ? File() : File (path);
}

void SharedState::setSplitFolder (const File& folder)
{
    settings->setValue (Keys::splitFolder, folder.getFullPathName());
    settings->saveIfNeeded();
    rescan();
}

File SharedState::getOwnFolder() const
{
    const auto path = settings->getValue (Keys::ownFolder);
    return path.isEmpty() ? File() : File (path);
}

void SharedState::setOwnFolder (const File& folder)
{
    settings->setValue (Keys::ownFolder, folder.getFullPathName());
    settings->saveIfNeeded();
    rescan();
}

void SharedState::useFolder (const File& folder, bool asOwn)
{
    IRLibrary probe;
    probe.scan (folder);
    if (asOwn || (! probe.has (IROrigin::Combined) && ! probe.has (IROrigin::Split)))
        setOwnFolder (folder);
    else if (probe.has (IROrigin::Split) && ! probe.has (IROrigin::Combined))
        setSplitFolder (folder);
    else
        setIRFolder (folder);
}

void SharedState::rescan()
{
    {
        const ScopedLock sl (libraryLock);
        library.scan (getIRFolder(), getSplitFolder(), getOwnFolder());
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

String SharedState::getPresetIdForErPattern (int pattern, IROrigin origin) const
{
    const ScopedLock sl (libraryLock);
    // OTHER takes the patterns of an M7 library (the combined one when both are there)
    if (origin == IROrigin::User || ! library.has (origin))
        origin = library.has (IROrigin::Combined) ? IROrigin::Combined : IROrigin::Split;
    const auto& list = library.getErPatternPresets (origin);
    if (! isPositiveAndNotGreaterThan (pattern, (int) list.size()) || pattern == 0) return {};
    return library.getPresets()[(size_t) list[(size_t) pattern - 1]].id;
}

String SharedState::getDefaultPresetId (IROrigin origin) const
{
    const ScopedLock sl (libraryLock);
    const auto first = library.firstPresetId (origin);
    if (first.isNotEmpty()) return first;
    return library.getPresets().empty() ? String() : library.getPresets().front().id;
}

String SharedState::getCounterpart (const String& id, IROrigin target) const
{
    const ScopedLock sl (libraryLock);
    return library.counterpart (id, target);
}

bool SharedState::hasLibrary (IROrigin origin) const
{
    const ScopedLock sl (libraryLock);
    return library.has (origin);
}

IROrigin SharedState::getPreferredOrigin() const
{
    const auto value = settings->getValue (Keys::library);
    return value == "split" ? IROrigin::Split : value == "own" ? IROrigin::User : IROrigin::Combined;
}

void SharedState::setPreferredOrigin (IROrigin origin)
{
    if (origin == getPreferredOrigin()) return;
    settings->setValue (Keys::library, origin == IROrigin::Split ? "split" : origin == IROrigin::User ? "own" : "combined");
    settings->saveIfNeeded();
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
