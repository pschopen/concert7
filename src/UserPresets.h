// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace concert7
{
/** One line of the preset browser: a library preset (an impulse response) or a user preset (a file with the
    whole sound: impulse response and every setting). */
struct PresetEntry
{
    juce::String key;          // library: the preset id ("Halls/01 Large Hall"); user: "user:" + path in the folder
    juce::String name;         // "Large Hall", or the user preset's name
    juce::String category;     // library category; user presets: their subfolder or kUserCategory
    juce::String number;       // "01" (library presets)
    juce::String irId;         // the impulse response it uses
    juce::StringArray tags;
    juce::String notes;
    juce::File file;           // user presets
    bool user = false;
    bool favourite = false;

    /** Every word of the search text appears in the name, category, number, tags, notes or impulse response. */
    bool matches (const juce::String& search) const;
};

/**
    User presets as files (*.c7preset) in the preset folder, plus favourites and tags of all presets.
    Tags of user presets live in their files, favourites and tags of library presets in the settings.
    Message thread only.

    File format: <Concert7Preset name tags notes version> with the processor state (the Concert7 tree of
    createStateTree()) as its only child.
*/
class UserPresetLibrary
{
public:
    static constexpr const char* kExtension = ".c7preset";
    static constexpr const char* kUserCategory = "User";
    static constexpr const char* kKeyPrefix = "user:";

    explicit UserPresetLibrary (juce::PropertiesFile& settings);

    /** ~/Library/Application Support/Concert7/Presets (macOS), %APPDATA%\Concert7\Presets, ~/.config/Concert7/Presets */
    juce::File getFolder() const;
    void rescan();
    const std::vector<PresetEntry>& getUserPresets() const { return presets; }
    const PresetEntry* find (const juce::String& key) const;
    static bool isUserKey (const juce::String& key) { return key.startsWith (kKeyPrefix); }

    /** Writes a preset (an existing one of that name is replaced); returns its key, empty on failure. */
    juce::String save (const juce::String& name, const juce::StringArray& tags, const juce::String& notes,
                       const juce::ValueTree& state, juce::String& error);
    /** The processor state stored in a user preset. */
    juce::ValueTree load (const juce::String& key) const;
    /** Moves the file to the trash. */
    bool remove (const juce::String& key);
    /** Renames a user preset and sets its tags and notes; returns the new key, empty on failure. */
    juce::String edit (const juce::String& key, const juce::String& newName, const juce::StringArray& tags,
                       const juce::String& notes, juce::String& error);

    bool isFavourite (const juce::String& key) const;
    void setFavourite (const juce::String& key, bool favourite);
    /** Tags of a library preset (user presets carry theirs in the file, see edit()). */
    juce::StringArray getLibraryTags (const juce::String& key) const;
    void setLibraryTags (const juce::String& key, const juce::StringArray& tags);

    /** "a, b,c" -> { "a", "b", "c" }: trimmed, without empty or repeated tags. */
    static juce::StringArray parseTags (const juce::String& text);

    /** Lets tests keep their presets somewhere else than the user's preset folder. */
    static juce::File& folderOverride();

private:
    juce::File fileFor (const juce::String& key) const;
    juce::String keyFor (const juce::File& file) const;
    bool write (const juce::File& file, const juce::String& name, const juce::StringArray& tags,
                const juce::String& notes, const juce::ValueTree& state, juce::String& error) const;

    juce::PropertiesFile& settings;
    std::vector<PresetEntry> presets;
};
} // namespace concert7
