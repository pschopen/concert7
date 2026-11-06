// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "UserPresets.h"

namespace concert7
{
using namespace juce;

namespace
{
    const Identifier presetTag ("Concert7Preset");
    const char* favouritesKey = "favourites";
    const char* libraryTagsKey = "presetTags";
}

bool PresetEntry::matches (const String& search) const
{
    const auto haystack = (name + " " + category + " " + number + " " + tags.joinIntoString (" ") + " " + notes + " "
                           + irId).toLowerCase();
    for (auto& word : StringArray::fromTokens (search.toLowerCase(), " ,", "\""))
        if (word.isNotEmpty() && ! haystack.contains (word))
            return false;
    return true;
}

//==============================================================================
UserPresetLibrary::UserPresetLibrary (PropertiesFile& s) : settings (s) {}

File& UserPresetLibrary::folderOverride()
{
    static File f;
    return f;
}

File UserPresetLibrary::getFolder() const
{
    if (folderOverride() != File())
        return folderOverride();
    auto base = File::getSpecialLocation (File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support"); // JUCE returns ~/Library on macOS
   #endif
    return base.getChildFile ("Concert7").getChildFile ("Presets");
}

String UserPresetLibrary::keyFor (const File& file) const
{
    return kKeyPrefix + file.getRelativePathFrom (getFolder()).replaceCharacter ('\\', '/');
}

File UserPresetLibrary::fileFor (const String& key) const
{
    if (! isUserKey (key)) return {};
    const auto relative = key.fromFirstOccurrenceOf (kKeyPrefix, false, false);
    if (relative.contains ("..")) return {};
    return getFolder().getChildFile (relative);
}

void UserPresetLibrary::rescan()
{
    presets.clear();
    const auto folder = getFolder();
    if (! folder.isDirectory()) return;
    for (const auto& item : RangedDirectoryIterator (folder, true, String ("*") + kExtension, File::findFiles))
    {
        const auto file = item.getFile();
        XmlDocument doc (file);
        // only the root element's attributes are needed for the list
        auto xml = doc.getDocumentElementIfTagMatches (presetTag.toString());
        if (xml == nullptr) continue;
        PresetEntry e;
        e.user = true;
        e.file = file;
        e.key = keyFor (file);
        e.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        e.tags = parseTags (xml->getStringAttribute ("tags"));
        e.notes = xml->getStringAttribute ("notes");
        e.category = file.getParentDirectory() == folder ? String (kUserCategory) : file.getParentDirectory().getFileName();
        if (auto* state = xml->getFirstChildElement())
            e.irId = state->getStringAttribute ("presetId");
        e.favourite = isFavourite (e.key);
        presets.push_back (std::move (e));
    }
    std::sort (presets.begin(), presets.end(), [] (const PresetEntry& a, const PresetEntry& b) {
        if (a.category != b.category) return a.category == kUserCategory || (b.category != kUserCategory && a.category < b.category);
        return a.name.compareNatural (b.name) < 0;
    });
}

const PresetEntry* UserPresetLibrary::find (const String& key) const
{
    for (auto& p : presets)
        if (p.key == key) return &p;
    return nullptr;
}

bool UserPresetLibrary::write (const File& file, const String& name, const StringArray& tags, const String& notes,
                               const ValueTree& state, String& error) const
{
    XmlElement root (presetTag);
    root.setAttribute ("name", name);
    root.setAttribute ("tags", tags.joinIntoString (", "));
    root.setAttribute ("notes", notes);
    root.setAttribute ("version", CONCERT7_VERSION);
    if (auto xml = state.createXml())
        root.addChildElement (xml.release());
    if (! file.getParentDirectory().createDirectory() || ! root.writeTo (file))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

String UserPresetLibrary::save (const String& name, const StringArray& tags, const String& notes, const ValueTree& state,
                                String& error)
{
    const auto clean = name.trim();
    if (clean.isEmpty())
    {
        error = "The preset needs a name";
        return {};
    }
    const auto file = getFolder().getChildFile (File::createLegalFileName (clean) + kExtension);
    if (! write (file, clean, parseTags (tags.joinIntoString (",")), notes, state, error))
        return {};
    rescan();
    return keyFor (file);
}

ValueTree UserPresetLibrary::load (const String& key) const
{
    const auto file = fileFor (key);
    if (! file.existsAsFile()) return {};
    auto xml = XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (presetTag.toString()) || xml->getFirstChildElement() == nullptr)
        return {};
    return ValueTree::fromXml (*xml->getFirstChildElement());
}

bool UserPresetLibrary::remove (const String& key)
{
    const auto file = fileFor (key);
    if (! file.existsAsFile() || ! file.moveToTrash()) return false;
    setFavourite (key, false);
    rescan();
    return true;
}

String UserPresetLibrary::edit (const String& key, const String& newName, const StringArray& tags, const String& notes,
                                String& error)
{
    const auto file = fileFor (key);
    const auto state = load (key);
    const auto clean = newName.trim();
    if (! state.isValid())
    {
        error = "Preset not found";
        return {};
    }
    if (clean.isEmpty())
    {
        error = "The preset needs a name";
        return {};
    }
    auto target = file;
    const auto* entry = find (key);
    if (entry == nullptr || entry->name != clean)
        target = file.getParentDirectory().getChildFile (File::createLegalFileName (clean) + kExtension);
    if (target != file && target.exists())
    {
        error = "A preset named \"" + clean + "\" exists already";
        return {};
    }
    if (! write (target, clean, tags, notes, state, error))
        return {};
    const bool favourite = isFavourite (key);
    if (target != file)
    {
        file.deleteFile(); // the preset now lives in the new file
        setFavourite (key, false);
        setFavourite (keyFor (target), favourite);
    }
    rescan();
    return keyFor (target);
}

bool UserPresetLibrary::isFavourite (const String& key) const
{
    return StringArray::fromLines (settings.getValue (favouritesKey)).contains (key);
}

void UserPresetLibrary::setFavourite (const String& key, bool favourite)
{
    auto list = StringArray::fromLines (settings.getValue (favouritesKey));
    list.removeEmptyStrings();
    if (favourite == list.contains (key)) return;
    if (favourite) list.add (key);
    else list.removeString (key);
    settings.setValue (favouritesKey, list.joinIntoString ("\n"));
    settings.saveIfNeeded();
    for (auto& p : presets)
        if (p.key == key) p.favourite = favourite;
}

StringArray UserPresetLibrary::getLibraryTags (const String& key) const
{
    const auto all = JSON::parse (settings.getValue (libraryTagsKey));
    return parseTags (all.getProperty (Identifier (key.isEmpty() ? String ("_") : key), {}).toString());
}

void UserPresetLibrary::setLibraryTags (const String& key, const StringArray& tags)
{
    auto all = JSON::parse (settings.getValue (libraryTagsKey));
    if (! all.isObject()) all = var (new DynamicObject());
    auto* obj = all.getDynamicObject();
    const auto clean = parseTags (tags.joinIntoString (","));
    if (clean.isEmpty()) obj->removeProperty (Identifier (key));
    else obj->setProperty (Identifier (key), clean.joinIntoString (", "));
    settings.setValue (libraryTagsKey, JSON::toString (all, true));
    settings.saveIfNeeded();
}

StringArray UserPresetLibrary::parseTags (const String& text)
{
    StringArray tags;
    for (auto& t : StringArray::fromTokens (text, ",;", "\""))
    {
        const auto tag = t.trim().toLowerCase();
        if (tag.isNotEmpty()) tags.addIfNotAlreadyThere (tag);
    }
    return tags;
}
} // namespace concert7
