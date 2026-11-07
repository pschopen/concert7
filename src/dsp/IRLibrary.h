// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace concert7
{
/** Where the impulse responses of a preset come from; the switch SAM / LS / OTHER chooses between them.
    Combined: a library of M7 presets with one response per preset (files named by bank and number).
    Split: a library of M7 presets with the early reflections and the late reverb recorded on their own (a folder
           per preset); the same presets as a combined library and more (the M7's second banks).
    User: any other impulse responses (OTHER), in categories by subfolder. */
enum class IROrigin { Combined = 0, Split, User };

/**
    One file set of a preset at one sample rate. A preset is either a 4-channel
    "quad" file (LL, LR, RL, RR), a pair of stereo files (left input / right
    input), a single mono/stereo file or, as split captures, the early reflections
    and the late reverb as files of their own (4 channels, one pair per take).
*/
struct IRVariant
{
    juce::File quad;
    juce::File left;
    juce::File right;
    juce::File single;
    std::vector<juce::File> heads, tails; // split captures: early reflections / late reverb, take 1 first

    bool isValid() const;
    bool isTrueStereo() const { return quad != juce::File() || (left != juce::File() && right != juce::File()) || ! heads.empty(); }
    bool hasBlocks() const { return ! heads.empty() && ! tails.empty(); }
    juce::int64 sizeOnDisk() const;
};

struct IRPreset
{
    juce::String id;          // stable identifier stored in sessions, e.g. "Halls/01 Large Hall", "Split/Halls2/05 Small Hall"
    juce::String category;    // display category, e.g. "Halls" (both banks of split captures), a subfolder for OTHER
    int categoryOrder = 99;   // numeric prefix of the category (1 = Halls ...), user IRs last
    int bank = 1;             // split captures: 2 for the M7's second banks (Halls 2 ...)
    int number = 0;           // number inside the category
    juce::String name;        // e.g. "Large Hall"
    IROrigin origin = IROrigin::User;
    std::map<int, IRVariant> variants; // keyed by sample rate in Hz

    /** Picks the best variant for a host rate (exact, integer multiple, then closest). */
    std::pair<int, IRVariant> bestVariantFor (double hostRate) const;
    /** "05", for the second banks "2.05". */
    juce::String displayNumber() const;
    /** "Halls", for the second banks "Halls 2". */
    juce::String bankName() const { return bank >= 2 ? category + " " + juce::String (bank) : category; }
};

/**
    Scans folders (recursively) for impulse responses. Understands M7 files named by
    bank and number ("1 Halls 01 Large Hall, 48K L.wav", "... 48K.wav" for quad files),
    split captures in a folder per preset ("Halls2/05 Small Hall/head-1.flac", "tail-1.flac")
    and treats any other audio file as an impulse response of its own (OTHER).
*/
class IRLibrary
{
public:
    static constexpr const char* kSplitPrefix = "Split/";

    struct ParsedName
    {
        bool ok = false;
        int categoryOrder = 0;
        juce::String category;
        int number = 0;
        juce::String name;
        int sampleRate = 0;
        juce::String side;   // "", "L" or "R"
    };

    static ParsedName parseFileName (const juce::String& fileNameWithoutExtension);

    /** A file of split captures: "<bank>/<NN Name>/head-<take>.flac" or "tail-<take>". */
    struct ParsedSplit
    {
        bool ok = false;
        bool head = false;
        int take = 0;
        juce::String category;   // "Halls" ... "Nonlin"
        int categoryOrder = 0;
        int bank = 1;
        int number = 0;
        juce::String name;
        juce::String id;
    };

    /** path: at least the last three parts ("Halls2/05 Small Hall/head-1.flac"), '/' or '\' separated. */
    static ParsedSplit parseSplitPath (const juce::String& path);

    /** Scans the folders synchronously (directory listing only, no audio is read). Every folder may hold any kind;
        the other impulse responses (OTHER) get their category from their first subfolder ("User" in the folder
        itself). */
    void scan (const juce::File& irFolder, const juce::File& splitFolder = {}, const juce::File& ownFolder = {});

    const std::vector<IRPreset>& getPresets() const { return presets; }
    /** Categories of one library, in the order of its presets. */
    juce::StringArray getCategories (IROrigin origin) const;
    int indexOf (const juce::String& presetId) const;
    const IRPreset* find (const juce::String& presetId) const;
    bool isEmpty() const { return presets.empty(); }
    bool has (IROrigin origin) const { return count (origin) > 0; }
    int count (IROrigin origin) const;
    int numBricastiPresets() const { return count (IROrigin::Combined); }
    juce::File getFolder() const { return folder; }
    juce::File getSplitFolder() const { return splitFolder; }
    juce::File getOwnFolder() const { return ownFolder; }
    /** The first preset of a library (empty when it is not installed). */
    juce::String firstPresetId (IROrigin origin) const;

    /** Presets used as the 32 selectable early reflection patterns of an M7 library, ordered from short/sparse to
        long/dense (pattern 1 is the first entry). */
    const std::vector<int>& getErPatternPresets (IROrigin origin = IROrigin::Combined) const;

    /** The same preset in the other M7 library (bank and number; the combined library's Spaces 17 is the split
        captures' Spaces 16, its 16 Europa has no counterpart). Empty when there is none. */
    juce::String counterpart (const juce::String& presetId, IROrigin target) const;
    static IROrigin originOfId (const juce::String& presetId);

    /** Display helper for category names ("Ambiences" -> "AMBIENCE"). */
    static juce::String shortCategoryName (const juce::String& category);

private:
    juce::File folder, splitFolder, ownFolder;
    std::vector<IRPreset> presets;
    std::vector<int> erPatternsCombined, erPatternsSplit;
};
} // namespace concert7
