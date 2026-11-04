// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "IRLibrary.h"
#include <regex>
#include <algorithm>
#include <cmath>

namespace concert7
{
using namespace juce;

bool IRVariant::isValid() const
{
    return quad.existsAsFile() || (left.existsAsFile() && right.existsAsFile()) || single.existsAsFile()
        || left.existsAsFile() || right.existsAsFile();
}

int64 IRVariant::sizeOnDisk() const
{
    if (quad != File()) return quad.getSize() / 4;
    if (left != File()) return left.getSize() / 2;
    if (right != File()) return right.getSize() / 2;
    return single.getSize();
}

std::pair<int, IRVariant> IRPreset::bestVariantFor (double hostRate) const
{
    if (variants.empty())
        return {};

    const int host = roundToInt (hostRate);
    if (auto it = variants.find (host); it != variants.end())
        return *it;

    // integer multiples resample cleanly (88.2k/176.4k from 44.1k, 96k/192k from 48k)
    for (auto& [rate, variant] : variants)
        if (rate > 0 && host % rate == 0)
            return { rate, variant };

    // otherwise the closest rate, preferring the higher one
    auto best = variants.begin();
    for (auto it = variants.begin(); it != variants.end(); ++it)
        if (std::abs (it->first - host) <= std::abs (best->first - host))
            best = it;
    return *best;
}

IRLibrary::ParsedName IRLibrary::parseFileName (const String& fileNameWithoutExtension)
{
    // "1 Halls 01 Large Hall, 48K L" / "6 Spaces 17 Gated Space, 44K"
    static const std::regex pattern (R"(^\s*(\d+)\s+([A-Za-z]+)\s+(\d+)\s+(.+?)\s*,\s*(\d+(?:\.\d+)?)\s*[Kk]\s*([LR])?\s*$)");
    ParsedName p;
    std::smatch m;
    const auto s = fileNameWithoutExtension.toStdString();
    if (! std::regex_match (s, m, pattern))
        return p;

    p.ok = true;
    p.categoryOrder = std::stoi (m[1].str());
    p.category = String (m[2].str());
    p.number = std::stoi (m[3].str());
    p.name = String (m[4].str()).trim();
    // "44K" means 44.1 kHz, "88K" 88.2 kHz and so on
    const int khz = (int) std::stod (m[5].str());
    p.sampleRate = (khz % 44 == 0) ? (khz / 44) * 44100 : khz * 1000;
    p.side = m[6].matched ? String (m[6].str()) : String();
    return p;
}

String IRLibrary::shortCategoryName (const String& category)
{
    auto c = category.toUpperCase();
    if (c == "AMBIENCES") return "AMBIENCE";
    return c;
}

void IRLibrary::scan (const File& newFolder)
{
    folder = newFolder;
    presets.clear();
    erPatterns.clear();
    bricastiCount = 0;

    if (! folder.isDirectory())
        return;

    std::map<String, IRPreset> byId;
    std::vector<IRPreset> userPresets;

    for (const auto& entry : RangedDirectoryIterator (folder, true, "*.wav;*.aif;*.aiff;*.flac", File::findFiles))
    {
        const auto file = entry.getFile();
        if (file.getFileName().startsWithChar ('.'))
            continue;

        const auto parsed = parseFileName (file.getFileNameWithoutExtension());
        if (parsed.ok)
        {
            const auto id = parsed.category + "/" + String (parsed.number).paddedLeft ('0', 2) + " " + parsed.name;
            auto& preset = byId[id];
            preset.id = id;
            preset.category = parsed.category;
            preset.categoryOrder = parsed.categoryOrder;
            preset.number = parsed.number;
            preset.name = parsed.name;

            auto& variant = preset.variants[parsed.sampleRate];
            if (parsed.side == "L")      variant.left = file;
            else if (parsed.side == "R") variant.right = file;
            else                         variant.quad = file;
        }
        else
        {
            IRPreset preset;
            preset.category = "User";
            preset.categoryOrder = 100;
            preset.name = file.getFileNameWithoutExtension();
            preset.id = "User/" + file.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
            preset.variants[0].single = file;
            userPresets.push_back (std::move (preset));
        }
    }

    for (auto& [id, preset] : byId)
        presets.push_back (std::move (preset));
    bricastiCount = (int) presets.size();

    std::sort (presets.begin(), presets.end(), [] (const IRPreset& a, const IRPreset& b) {
        if (a.categoryOrder != b.categoryOrder) return a.categoryOrder < b.categoryOrder;
        if (a.category != b.category) return a.category < b.category;
        return a.number < b.number;
    });

    std::sort (userPresets.begin(), userPresets.end(), [] (const IRPreset& a, const IRPreset& b) {
        return a.id.compareNatural (b.id) < 0;
    });
    int n = 1;
    for (auto& p : userPresets)
    {
        p.number = n++;
        presets.push_back (std::move (p));
    }

    // Early reflection patterns: spread the factory presets from short to long.
    // The file size is a cheap and reliable proxy for the length of the response.
    std::vector<std::pair<int64, int>> bySize;
    for (int i = 0; i < bricastiCount; ++i)
    {
        auto& variants = presets[(size_t) i].variants;
        if (! variants.empty())
            bySize.push_back ({ variants.begin()->second.sizeOnDisk(), i });
    }
    std::sort (bySize.begin(), bySize.end());
    const int count = (int) bySize.size();
    const int numPatterns = std::min (count, 32);
    for (int k = 0; k < numPatterns; ++k)
    {
        const int idx = numPatterns > 1 ? (int) std::lround ((double) k * (count - 1) / (numPatterns - 1)) : 0;
        erPatterns.push_back (bySize[(size_t) idx].second);
    }
}

StringArray IRLibrary::getCategories() const
{
    StringArray cats;
    for (auto& p : presets)
        cats.addIfNotAlreadyThere (p.category);
    return cats;
}

int IRLibrary::indexOf (const String& presetId) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].id == presetId)
            return (int) i;
    return -1;
}

const IRPreset* IRLibrary::find (const String& presetId) const
{
    const int i = indexOf (presetId);
    return i >= 0 ? &presets[(size_t) i] : nullptr;
}
} // namespace concert7
