// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "IRLibrary.h"
#include <regex>
#include <algorithm>
#include <cmath>
#include <set>

namespace concert7
{
using namespace juce;

bool IRVariant::isValid() const
{
    if (hasBlocks())
        return heads.front().existsAsFile() && tails.front().existsAsFile();
    return quad.existsAsFile() || (left.existsAsFile() && right.existsAsFile()) || single.existsAsFile()
        || left.existsAsFile() || right.existsAsFile();
}

int64 IRVariant::sizeOnDisk() const
{
    // split captures: the early reflections alone (what the early patterns are sorted by)
    if (! heads.empty()) return heads.front().getSize() / 4;
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

String IRPreset::displayNumber() const
{
    const auto n = String (number).paddedLeft ('0', 2);
    return bank >= 2 ? String (bank) + "." + n : n;
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

IRLibrary::ParsedSplit IRLibrary::parseSplitPath (const String& path)
{
    // ".../Halls2/05 Small Hall/head-1.flac": the bank, the preset, the block and its take
    static const std::regex filePattern (R"(^(head|tail)-(\d+)\.(flac|wav|aif|aiff)$)", std::regex::icase);
    static const std::regex presetPattern (R"(^(\d+)\s+(.+?)\s*$)");
    static const std::regex bankPattern (R"(^(Halls|Plates|Rooms|Chambers|Ambience|Spaces)([12])$|^(Nonlin)$)");
    ParsedSplit p;
    auto parts = StringArray::fromTokens (path.replaceCharacter ('\\', '/'), "/", {});
    parts.removeEmptyStrings();
    if (parts.size() < 3) return p;
    std::smatch f, pr, b;
    const auto file = parts[parts.size() - 1].toStdString(), preset = parts[parts.size() - 2].toStdString(),
               bank = parts[parts.size() - 3].toStdString();
    if (! std::regex_match (file, f, filePattern) || ! std::regex_match (preset, pr, presetPattern)
        || ! std::regex_match (bank, b, bankPattern))
        return p;

    static const char* order[] = { "Halls", "Plates", "Rooms", "Chambers", "Ambiences", "Spaces", "Nonlin" };
    p.ok = true;
    p.head = String (f[1].str()).equalsIgnoreCase ("head");
    p.take = std::stoi (f[2].str());
    p.number = std::stoi (pr[1].str());
    p.name = String (pr[2].str()).trim();
    if (b[3].matched)
    {
        p.category = "Nonlin";
        p.bank = 1;
    }
    else
    {
        // the same category names as the combined library's ("Ambience1" holds the ambiences)
        p.category = b[1].str() == "Ambience" ? String ("Ambiences") : String (b[1].str());
        p.bank = std::stoi (b[2].str());
    }
    for (int i = 0; i < 7; ++i)
        if (p.category == order[i]) p.categoryOrder = i + 1;
    p.id = String (kSplitPrefix) + String (bank) + "/" + String (p.number).paddedLeft ('0', 2) + " " + p.name;
    return p;
}

IROrigin IRLibrary::originOfId (const String& presetId)
{
    if (presetId.startsWith (kSplitPrefix)) return IROrigin::Split;
    if (presetId.startsWith ("User/")) return IROrigin::User;
    return IROrigin::Combined;
}

String IRLibrary::shortCategoryName (const String& category)
{
    auto c = category.toUpperCase();
    if (c == "AMBIENCES") return "AMBIENCE";
    return c;
}

void IRLibrary::scan (const File& newFolder, const File& newSplitFolder, const File& newOwnFolder)
{
    folder = newFolder;
    splitFolder = newSplitFolder;
    ownFolder = newOwnFolder;
    presets.clear();
    erPatternsCombined.clear();
    erPatternsSplit.clear();

    std::map<String, IRPreset> byId, splitById;
    std::vector<IRPreset> userPresets;
    std::set<String> userIds;

    // split captures: one preset per folder, early reflections and late reverb per take
    auto addSplit = [&splitById] (const File& file, const ParsedSplit& parsed) {
        auto& preset = splitById[parsed.id];
        if (preset.id.isEmpty())
        {
            preset.id = parsed.id;
            preset.category = parsed.category;
            preset.categoryOrder = parsed.categoryOrder;
            preset.bank = parsed.bank;
            preset.number = parsed.number;
            preset.name = parsed.name;
            preset.origin = IROrigin::Split;
        }
        auto& variant = preset.variants[0]; // the rate is that of the files, read when loading
        auto& list = parsed.head ? variant.heads : variant.tails;
        if ((int) list.size() < parsed.take) list.resize ((size_t) parsed.take);
        if (list[(size_t) parsed.take - 1] == File())
            list[(size_t) parsed.take - 1] = file;
    };
    auto relativeSplitPath = [] (const File& file) {
        return file.getParentDirectory().getParentDirectory().getFileName() + "/" + file.getParentDirectory().getFileName()
             + "/" + file.getFileName();
    };

    // every folder once (one inside another is scanned with it)
    std::vector<File> roots;
    for (auto& f : { folder, splitFolder, ownFolder })
        if (f.isDirectory())
        {
            bool covered = false;
            for (auto& r : roots) covered = covered || f == r || f.isAChildOf (r);
            if (covered) continue;
            roots.erase (std::remove_if (roots.begin(), roots.end(), [&f] (const File& r) { return r.isAChildOf (f); }), roots.end());
            roots.push_back (f);
        }

    for (auto& root : roots)
        for (const auto& entry : RangedDirectoryIterator (root, true, "*.wav;*.aif;*.aiff;*.flac", File::findFiles))
        {
            const auto file = entry.getFile();
            if (file.getFileName().startsWithChar ('.'))
                continue;

            if (const auto split = parseSplitPath (relativeSplitPath (file)); split.ok)
            {
                addSplit (file, split);
                continue;
            }
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
                preset.origin = IROrigin::Combined;

                auto& variant = preset.variants[parsed.sampleRate];
                if (parsed.side == "L")      variant.left = file;
                else if (parsed.side == "R") variant.right = file;
                else                         variant.quad = file;
            }
            else
            {
                // any other impulse response: its first subfolder is its category
                const auto relative = file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
                IRPreset preset;
                preset.category = relative.containsChar ('/') ? relative.upToFirstOccurrenceOf ("/", false, false) : String ("User");
                preset.categoryOrder = 100;
                preset.name = file.getFileNameWithoutExtension();
                preset.id = "User/" + relative;
                preset.variants[0].single = file;
                if (userIds.insert (preset.id).second)
                    userPresets.push_back (std::move (preset));
            }
        }

    auto byOrder = [] (const IRPreset& a, const IRPreset& b) {
        if (a.categoryOrder != b.categoryOrder) return a.categoryOrder < b.categoryOrder;
        if (a.category != b.category) return a.category < b.category;
        if (a.bank != b.bank) return a.bank < b.bank;
        return a.number < b.number;
    };
    for (auto& [id, preset] : byId)
        presets.push_back (std::move (preset));
    std::sort (presets.begin(), presets.end(), byOrder);

    // a capture without both blocks of take 1 is incomplete (an interrupted extraction)
    std::vector<IRPreset> split;
    for (auto& [id, preset] : splitById)
    {
        auto& v = preset.variants[0];
        auto missing = [] (const File& f) { return f == File(); };
        if (v.heads.empty() || v.tails.empty() || missing (v.heads.front()) || missing (v.tails.front()))
            continue;
        // takes only where both blocks are there, consecutive from take 1
        size_t takes = std::min (v.heads.size(), v.tails.size());
        for (size_t t = 0; t < takes; ++t)
            if (missing (v.heads[t]) || missing (v.tails[t])) { takes = t; break; }
        v.heads.resize (takes);
        v.tails.resize (takes);
        split.push_back (std::move (preset));
    }
    std::sort (split.begin(), split.end(), byOrder);
    for (auto& p : split)
        presets.push_back (std::move (p));

    // the user's own: by category ("User" first), numbered within it
    std::sort (userPresets.begin(), userPresets.end(), [] (const IRPreset& a, const IRPreset& b) {
        if (a.category != b.category)
            return a.category == "User" || (b.category != "User" && a.category.compareNatural (b.category) < 0);
        return a.id.compareNatural (b.id) < 0;
    });
    std::map<String, int> numbers;
    for (auto& p : userPresets)
    {
        p.number = ++numbers[p.category];
        presets.push_back (std::move (p));
    }

    // Early reflection patterns: spread each library's presets from short to long.
    // The file size is a cheap and reliable proxy for the length of the response (split captures: of its reflections).
    for (auto origin : { IROrigin::Combined, IROrigin::Split })
    {
        auto& erPatterns = origin == IROrigin::Combined ? erPatternsCombined : erPatternsSplit;
        std::vector<std::pair<int64, int>> bySize;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            auto& variants = presets[(size_t) i].variants;
            if (presets[(size_t) i].origin == origin && ! variants.empty())
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
}

int IRLibrary::count (IROrigin origin) const
{
    return (int) std::count_if (presets.begin(), presets.end(), [origin] (const IRPreset& p) { return p.origin == origin; });
}

String IRLibrary::firstPresetId (IROrigin origin) const
{
    for (auto& p : presets)
        if (p.origin == origin) return p.id;
    return {};
}

const std::vector<int>& IRLibrary::getErPatternPresets (IROrigin origin) const
{
    return origin == IROrigin::Split ? erPatternsSplit : erPatternsCombined;
}

String IRLibrary::counterpart (const String& presetId, IROrigin target) const
{
    const auto* p = find (presetId);
    if (p == nullptr || p->origin == IROrigin::User || target == IROrigin::User) return {};
    if (p->origin == target) return p->id;
    // the combined library has the first banks only; its Spaces are numbered one higher from 16 on (16 Europa is its own)
    const bool spaces = p->category == "Spaces";
    int number = p->number;
    if (target == IROrigin::Split)
    {
        if (spaces && number == 16) return {};
        if (spaces && number > 16) --number;
    }
    else
    {
        if (p->bank != 1 || p->category == "Nonlin") return {};
        if (spaces && number >= 16) ++number;
    }
    for (auto& q : presets)
        if (q.origin == target && q.category == p->category && q.bank == 1 && q.number == number)
            return q.id;
    return {};
}

StringArray IRLibrary::getCategories (IROrigin origin) const
{
    StringArray cats;
    for (auto& p : presets)
        if (p.origin == origin) cats.addIfNotAlreadyThere (p.category);
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
