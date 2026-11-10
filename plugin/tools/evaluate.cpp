// =============================================================================
// evaluate.cpp
//
// Command-line test for the finished pipeline. It plays .wav files through
// the exact Engine the plugin uses (in small blocks, like a DAW would) and
// prints every hit it recognises.
//
// Usage:
//   evaluate <thresholdDb> <file.wav> [more.wav ...]
//
// Output (tab separated, one line per hit):
//   file <tab> timeMs <tab> class <tab> confidence <tab> peakDb
// A file with no hits prints one line with class "NONE".
// =============================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../core/Engine.h"
#include "WavFile.h"

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: evaluate <thresholdDb> <file.wav>...\n");
        return 2;
    }

    const float thresholdDb = (float) std::atof (argv[1]);

    for (int arg = 2; arg < argc; ++arg)
    {
        const std::string path = argv[arg];

        kushaya::WavData wav;
        if (! kushaya::readWavFile (path, wav))
        {
            std::fprintf (stderr, "SKIP (cannot read): %s\n", path.c_str());
            continue;
        }

        kushaya::Engine engine;
        engine.prepare (wav.sampleRate);
        engine.setThresholdDb (thresholdDb);

        int numHits = 0;
        kushaya::Hit hit;

        for (size_t i = 0; i < wav.samples.size(); ++i)
        {
            if (engine.pushSample (wav.samples[i], hit))
            {
                ++numHits;
                std::printf ("%s\t%.1f\t%s\t%.2f\t%.1f\n",
                             path.c_str(),
                             1000.0 * (double) i / wav.sampleRate,
                             kushaya::model::classNames[hit.classIndex],
                             hit.confidence,
                             20.0 * std::log10 (hit.peak + 1.0e-9));
            }
        }

        if (numHits == 0)
            std::printf ("%s\t0\tNONE\t0\t0\n", path.c_str());
    }

    return 0;
}
