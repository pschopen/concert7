// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "WIRFile.h"

namespace concert7
{
using namespace juce;

namespace
{
    uint32 readLE32 (const uint8* p) { return ByteOrder::littleEndianInt (p); }
    uint16 readLE16 (const uint8* p) { return ByteOrder::littleEndianShort (p); }

    /** The "fmt " chunk of a RIFF-like file (its data, at least 16 bytes), nullptr when there is none. */
    uint8* findFormatChunk (uint8* data, size_t size)
    {
        size_t pos = 12;
        while (pos + 8 <= size)
        {
            const auto chunkSize = (size_t) readLE32 (data + pos + 4);
            if (std::memcmp (data + pos, "fmt ", 4) == 0)
                return chunkSize >= 16 && pos + 8 + 16 <= size ? data + pos + 8 : nullptr;
            pos += 8 + chunkSize + (chunkSize & 1);
        }
        return nullptr;
    }
} // namespace

WIRAudioFormat::WIRAudioFormat() : AudioFormat ("WIR file", ".wir") {}

bool WIRAudioFormat::toWav (MemoryBlock& block)
{
    auto* data = static_cast<uint8*> (block.getData());
    if (block.getSize() < 12) return false;
    if (std::memcmp (data, "RIFF", 4) == 0 && std::memcmp (data + 8, "WAVE", 4) == 0) return true;
    if (std::memcmp (data, "wvIR", 4) != 0) return false;

    std::memcpy (data, "RIFF", 4);
    std::memcpy (data + 8, "WAVE", 4);
    // float samples marked as 23 bits (the mantissa): 32 bit float for the WAV reader
    if (auto* fmt = findFormatChunk (data, block.getSize()); fmt != nullptr && readLE16 (fmt + 14) == 23)
    {
        fmt[14] = 32;
        fmt[15] = 0;
    }
    return true;
}

int WIRAudioFormat::numChannels (const File& file)
{
    FileInputStream in (file);
    if (! in.openedOk()) return 0;
    MemoryBlock head;
    in.readIntoMemoryBlock (head, 4096);
    if (! toWav (head)) return 0;
    const auto* fmt = findFormatChunk (static_cast<uint8*> (head.getData()), head.getSize());
    return fmt != nullptr ? (int) readLE16 (fmt + 2) : 0;
}

AudioFormatReader* WIRAudioFormat::createReaderFor (InputStream* sourceStream, bool deleteStreamIfOpeningFails)
{
    std::unique_ptr<InputStream> owned (sourceStream);
    MemoryBlock data;
    sourceStream->readIntoMemoryBlock (data);
    if (! toWav (data))
    {
        if (! deleteStreamIfOpeningFails) owned.release();
        return nullptr;
    }
    // the reader owns the stream with the corrected copy; the file's stream is no longer needed
    WavAudioFormat wav;
    return wav.createReaderFor (new MemoryInputStream (std::move (data)), true);
}

std::map<String, WIRPosition> readWIRPositions (const File& folder)
{
    std::map<String, WIRPosition> positions;
    for (const auto& entry : RangedDirectoryIterator (folder, false, "*.xps", File::findFiles))
    {
        const auto xml = XmlDocument::parse (entry.getFile());
        if (xml == nullptr) continue;
        // every response a preset names, with the descriptors next to it
        std::function<void (const XmlElement&)> visit = [&] (const XmlElement& e) {
            StringPairArray d;
            for (auto* c : e.getChildWithTagNameIterator ("Descriptors"))
                for (auto* x : c->getChildWithTagNameIterator ("Descriptor"))
                    d.set (x->getStringAttribute ("Name"), x->getAllSubText().trim());
            if (const auto file = d["IRFileNameFull"]; file.isNotEmpty())
            {
                auto& p = positions[file.toLowerCase()];
                if (p.location.isEmpty()) p.location = d["MicLocation"];
                if (p.distance <= 0.0f) p.distance = d["Distance"].getFloatValue();
            }
            for (auto* c : e.getChildIterator())
                visit (*c);
        };
        visit (*xml);
    }
    return positions;
}

void registerIRFormats (AudioFormatManager& fm)
{
    fm.registerBasicFormats();
    fm.registerFormat (new WIRAudioFormat(), false);
}
} // namespace concert7
