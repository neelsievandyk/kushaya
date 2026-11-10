// =============================================================================
// WavFile.h
//
// A minimal .wav reader for the command-line tools (the plugin itself never
// reads files). Handles the formats a DAW normally exports: 16/24/32-bit
// integer and 32-bit float. Multi-channel files are mixed down to mono.
// =============================================================================

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace kushaya
{

struct WavData
{
    double sampleRate = 0.0;
    std::vector<float> samples; // mono, -1..1
};

// Returns false (and leaves `out` empty) if the file cannot be read.
inline bool readWavFile (const std::string& path, WavData& out)
{
    FILE* file = std::fopen (path.c_str(), "rb");
    if (file == nullptr)
        return false;

    std::vector<unsigned char> bytes;
    unsigned char chunk[65536];
    size_t got = 0;
    while ((got = std::fread (chunk, 1, sizeof (chunk), file)) > 0)
        bytes.insert (bytes.end(), chunk, chunk + got);
    std::fclose (file);

    auto read16 = [&] (size_t p) { return (uint32_t) bytes[p] | ((uint32_t) bytes[p + 1] << 8); };
    auto read32 = [&] (size_t p) { return read16 (p) | (read16 (p + 2) << 16); };

    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) != 0
        || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    uint32_t format = 0, channels = 0, bitsPerSample = 0, sampleRate = 0;
    size_t dataStart = 0, dataSize = 0;

    // A wav file is a list of named chunks. We need "fmt " and "data".
    for (size_t p = 12; p + 8 <= bytes.size();)
    {
        const uint32_t size = read32 (p + 4);
        const size_t body = p + 8;

        if (std::memcmp (bytes.data() + p, "fmt ", 4) == 0 && body + 16 <= bytes.size())
        {
            format        = read16 (body);
            channels      = read16 (body + 2);
            sampleRate    = read32 (body + 4);
            bitsPerSample = read16 (body + 14);

            // "Extensible" files keep the real format code further in.
            if (format == 0xFFFE && size >= 26 && body + 26 <= bytes.size())
                format = read16 (body + 24);
        }
        else if (std::memcmp (bytes.data() + p, "data", 4) == 0)
        {
            dataStart = body;
            dataSize  = std::min ((size_t) size, bytes.size() - body);
            break;
        }

        p = body + size + (size & 1); // chunks are padded to an even length
    }

    const bool isInt   = format == 1 && (bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32);
    const bool isFloat = format == 3 && bitsPerSample == 32;

    if (dataStart == 0 || channels == 0 || ! (isInt || isFloat))
        return false;

    const size_t bytesPerSample = bitsPerSample / 8;
    const size_t numFrames = dataSize / (bytesPerSample * channels);

    out.sampleRate = (double) sampleRate;
    out.samples.assign (numFrames, 0.0f);

    for (size_t frame = 0; frame < numFrames; ++frame)
    {
        double sum = 0.0;

        for (uint32_t ch = 0; ch < channels; ++ch)
        {
            const size_t p = dataStart + (frame * channels + ch) * bytesPerSample;

            if (isFloat)
            {
                float value;
                std::memcpy (&value, bytes.data() + p, 4);
                sum += value;
            }
            else if (bitsPerSample == 16)
            {
                sum += (int16_t) read16 (p) / 32768.0;
            }
            else if (bitsPerSample == 24)
            {
                int32_t value = (int32_t) (read16 (p) | ((uint32_t) bytes[p + 2] << 16));
                if (value & 0x800000)
                    value -= 0x1000000; // sign-extend 24 bits
                sum += value / 8388608.0;
            }
            else
            {
                sum += (int32_t) read32 (p) / 2147483648.0;
            }
        }

        out.samples[frame] = (float) (sum / channels);
    }

    return true;
}

} // namespace kushaya
