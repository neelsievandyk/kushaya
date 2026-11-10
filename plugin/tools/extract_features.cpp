// =============================================================================
// extract_features.cpp
//
// Command-line tool used by scripts/train_plugin_model.py.
//
// For every .wav file given, it finds the beatbox hit using the SAME onset
// detector the plugin uses, cuts out the SAME short slice the plugin would
// cut out, and prints the SAME 18 features the plugin would compute.
// The Python script then trains the model on those numbers.
//
// Usage:
//   extract_features <windowMs> <thresholdDb> <label> <file.wav> [more.wav ...]
//
// Output (one line per slice, comma separated):
//   label,file,shiftMs,feature0,...,feature17
//
// Each file produces several lines, with the slice shifted a little earlier
// or later ("shiftMs"). In real use the trigger point moves around by a
// millisecond or two depending on how loud the hit is, so training on the
// shifted copies teaches the model not to care.
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../core/FeatureExtractor.h"
#include "../core/OnsetCapture.h"
#include "WavFile.h"

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: extract_features <windowMs> <thresholdDb> <label> <file.wav>...\n");
        return 2;
    }

    const float windowMs    = (float) std::atof (argv[1]);
    const float thresholdDb = (float) std::atof (argv[2]);
    const std::string label = argv[3];

    // Set the environment variable KUSHAYA_VERBOSE=1 for extra detail.
    const bool verbose = std::getenv ("KUSHAYA_VERBOSE") != nullptr;

    const float shiftsMs[] = { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.5f };

    for (int arg = 4; arg < argc; ++arg)
    {
        const std::string path = argv[arg];

        kushaya::WavData wav;
        if (! kushaya::readWavFile (path, wav))
        {
            std::fprintf (stderr, "SKIP (cannot read): %s\n", path.c_str());
            continue;
        }

        kushaya::OnsetCapture capture;
        capture.prepare (wav.sampleRate, windowMs);
        capture.setThresholdDb (thresholdDb);

        kushaya::FeatureExtractor extractor;
        extractor.prepare (wav.sampleRate, windowMs);

        // Run the detector over the whole file and note where each slice starts.
        // A file often holds a small click or breath as well as the real hit,
        // so we remember which slice was the loudest and train on that one.
        std::vector<long> sliceStarts;
        const long numSamples = (long) wav.samples.size();
        long loudestStart = 0;
        float loudestPeak = -1.0f;

        for (long i = 0; i < numSamples; ++i)
        {
            if (capture.pushSample (wav.samples[(size_t) i]))
            {
                const long start = i + 1 - capture.getWindowSize();
                sliceStarts.push_back (start);

                if (capture.getWindowPeak() > loudestPeak)
                {
                    loudestPeak = capture.getWindowPeak();
                    loudestStart = start;
                }
            }
        }

        if (sliceStarts.empty())
        {
            std::fprintf (stderr, "SKIP (no hit found): %s\n", path.c_str());
            continue;
        }

        // Printed with --verbose only: where every hit in the file was found.
        if (verbose && sliceStarts.size() > 1)
        {
            std::fprintf (stderr, "NOTE (%d hits, using the loudest at %.0f ms; all at",
                          (int) sliceStarts.size(), 1000.0 * (double) loudestStart / wav.sampleRate);
            for (long start : sliceStarts)
                std::fprintf (stderr, " %.0f", 1000.0 * (double) start / wav.sampleRate);
            std::fprintf (stderr, " ms): %s\n", path.c_str());
        }

        const int size = capture.getWindowSize();
        std::vector<float> slice ((size_t) size);
        float features[kushaya::FeatureExtractor::numFeatures];

        for (float shiftMs : shiftsMs)
        {
            const long start = loudestStart + std::lround (shiftMs * 0.001 * wav.sampleRate);

            // Copy the shifted slice; anything outside the file counts as silence.
            for (int i = 0; i < size; ++i)
            {
                const long index = start + i;
                slice[(size_t) i] = (index >= 0 && index < numSamples) ? wav.samples[(size_t) index] : 0.0f;
            }

            extractor.extract (slice.data(), features);

            std::printf ("%s,\"%s\",%.1f", label.c_str(), path.c_str(), shiftMs);
            for (float f : features)
                std::printf (",%.9g", f);
            std::printf ("\n");
        }
    }

    return 0;
}
