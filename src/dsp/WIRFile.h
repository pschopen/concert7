// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace concert7
{
/**
    Impulse responses of the WIR library (.wir). A .wir file is a WAV file with its header renamed ("wvIR" and
    "ver1" for "RIFF" and "WAVE") and 32 bit float samples marked as 23 bits; a few are plain WAV files. The reader
    puts the header back and reads the file with JUCE's WAV reader.
*/
class WIRAudioFormat : public juce::AudioFormat
{
public:
    WIRAudioFormat();

    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }

    juce::AudioFormatReader* createReaderFor (juce::InputStream* sourceStream, bool deleteStreamIfOpeningFails) override;
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor (std::unique_ptr<juce::OutputStream>&,
                                                              const juce::AudioFormatWriterOptions&) override
    {
        return nullptr;
    }
    using juce::AudioFormat::createWriterFor;

    /** The header of a .wir file made that of a WAV file, in place. False when it is neither. */
    static bool toWav (juce::MemoryBlock& data);

    /** The number of channels of a .wir (or WAV) file from its header alone, 0 when it cannot be read. */
    static int numChannels (const juce::File& file);
};

/** Where a response of the WIR library was recorded, from the presets (.xps) next to it. */
struct WIRPosition
{
    juce::String location;   // "Row 12 Seat 20"
    float distance = 0.0f;   // metres from the source, 0 when not given
};

/** The positions the presets (.xps) in a folder give for its .wir files, keyed by file name in lower case. */
std::map<juce::String, WIRPosition> readWIRPositions (const juce::File& folder);

/** The formats of every impulse response Concert7 reads: WAV, AIFF, FLAC (and the rest of JUCE's basic formats) and .wir. */
void registerIRFormats (juce::AudioFormatManager& fm);
} // namespace concert7
