// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace concert7
{
/**
    One file set of a preset at one sample rate. A preset is either a 4-channel
    "quad" file (LL, LR, RL, RR), a pair of stereo files (left input / right
    input) or a single mono/stereo file.
*/
struct IRVariant
{
    juce::File quad;
    juce::File left;
    juce::File right;
    juce::File single;

    bool isValid() const;
    bool isTrueStereo() const { return quad != juce::File() || (left != juce::File() && right != juce::File()); }
    juce::int64 sizeOnDisk() const;
};

struct IRPreset
{
    juce::String id;          // stable identifier stored in sessions, e.g. "Halls/01 Large Hall"
    juce::String category;    // display category, e.g. "Halls"
    int categoryOrder = 99;   // numeric prefix of the category (1 = Halls ...), user IRs last
    int number = 0;           // number inside the category
    juce::String name;        // e.g. "Large Hall"
    std::map<int, IRVariant> variants; // keyed by sample rate in Hz

    /** Picks the best variant for a host rate (exact, integer multiple, then closest). */
    std::pair<int, IRVariant> bestVariantFor (double hostRate) const;
    juce::String displayNumber() const { return juce::String (number).paddedLeft ('0', 2); }
};

/**
    Scans a folder (recursively) for impulse responses. Understands the file naming
    of the Samplicity Bricasti M7 library ("1 Halls 01 Large Hall, 48K L.wav",
    "... 48K.wav" for quad files) and falls back to treating any other audio file
    as a user impulse response.
*/
class IRLibrary
{
public:
    /** Samplicity's downloads page; the library is listed at the bottom ("Bricasti M7 impulse response archive"). */
    static constexpr const char* kDownloadPage = "https://samplicity.com/downloads/";

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

    /** Scans the folder synchronously (directory listing only, no audio is read). */
    void scan (const juce::File& folder);

    const std::vector<IRPreset>& getPresets() const { return presets; }
    juce::StringArray getCategories() const;
    int indexOf (const juce::String& presetId) const;
    const IRPreset* find (const juce::String& presetId) const;
    bool isEmpty() const { return presets.empty(); }
    int numBricastiPresets() const { return bricastiCount; }
    juce::File getFolder() const { return folder; }

    /** Presets used as the 32 selectable early reflection patterns, ordered
        from short/sparse to long/dense (pattern 1 is the first entry). */
    const std::vector<int>& getErPatternPresets() const { return erPatterns; }

    /** Display helper for category names ("Ambiences" -> "AMBIENCE"). */
    static juce::String shortCategoryName (const juce::String& category);

private:
    juce::File folder;
    std::vector<IRPreset> presets;
    std::vector<int> erPatterns;
    int bricastiCount = 0;
};
} // namespace concert7
