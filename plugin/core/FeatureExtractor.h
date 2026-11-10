// =============================================================================
// FeatureExtractor.h
//
// Step 2 of the pipeline: turn a short slice of audio into a small list of
// numbers ("features") that describe what it sounds like.
//
// The classifier never sees raw audio. It only sees these 18 numbers:
//
//    0..12  MFCCs       - the overall shape of the spectrum (13 numbers)
//    13     centroid    - "brightness": the average frequency, in kHz
//    14     rolloff     - frequency below which 85% of the energy sits, in kHz
//    15     flatness    - 0 = tonal (a clear pitch), 1 = pure noise
//    16     zero-crossing rate - how often the wave crosses zero, per ms
//    17     tail ratio  - is the 2nd half of the slice louder or quieter
//                         than the 1st half? (how fast the sound dies away)
//
// IMPORTANT: this exact code is used both to TRAIN the model (through
// tools/extract_features.cpp) and to RUN it inside the plugin. That is what
// guarantees the plugin sees the same numbers the model was trained on.
// If you change anything here, you must retrain (scripts/retrain.sh).
//
// Two design choices worth knowing about:
//   - The slice is scaled to a standard loudness first, so a quiet kick and
//     a loud kick give the same features. Loudness is handled separately
//     (it becomes the MIDI velocity).
//   - Frequencies are worked out in Hz, not in "FFT bins", so the features
//     come out the same whatever sample rate the DAW is running at.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "Fft.h"

namespace kushaya
{

class FeatureExtractor
{
public:
    static constexpr int numMelBands = 24;
    static constexpr int numMfcc     = 13;
    static constexpr int numFeatures = numMfcc + 5;

    // Call once before use, and again if the sample rate changes.
    // windowMs must match the length of the slices you will pass to extract().
    void prepare (double sampleRate, float windowMs)
    {
        const double pi = 3.14159265358979323846;

        windowSamples = std::max (16, (int) std::lround (windowMs * 0.001 * sampleRate));
        samplesPerMs  = sampleRate / 1000.0;

        // FFT size: at least twice the window (the rest is padded with
        // zeros), which gives a smoother spectrum.
        fftSize = nextPowerOfTwo (windowSamples) * 2;
        fftData.assign ((size_t) fftSize, std::complex<double> (0.0, 0.0));
        scratch.assign ((size_t) windowSamples, 0.0);

        numBins = fftSize / 2 + 1;
        binHz   = sampleRate / (double) fftSize;
        power.assign ((size_t) numBins, 0.0);

        // Only analyse up to 12 kHz (or a bit under the highest frequency
        // the sample rate can represent, if that is lower).
        maxHz  = std::min (12000.0, 0.45 * sampleRate);
        maxBin = std::min (numBins - 1, (int) std::floor (maxHz / binHz));

        // --- Fade shape applied to the slice before the FFT -----------------
        // A short fade-in and a longer fade-out stop the hard edges of the
        // slice from showing up as fake high frequencies. The middle is left
        // untouched so the attack of the hit is kept at full strength.
        fade.assign ((size_t) windowSamples, 1.0);
        const int fadeIn  = std::max (1, (int) std::lround (0.001 * sampleRate)); // 1 ms
        const int fadeOut = std::max (1, windowSamples / 4);

        for (int i = 0; i < windowSamples; ++i)
        {
            if (i < fadeIn)
                fade[(size_t) i] = 0.5 - 0.5 * std::cos (pi * (i + 0.5) / fadeIn);

            const int fromEnd = windowSamples - 1 - i;
            if (fromEnd < fadeOut)
                fade[(size_t) i] *= 0.5 - 0.5 * std::cos (pi * (fromEnd + 0.5) / fadeOut);
        }

        // --- Mel filter bank -------------------------------------------------
        // 24 overlapping triangular "listening bands" spaced the way human
        // hearing works: narrow at low frequencies, wide at high ones.
        // melWeights[band][bin] says how much FFT bin `bin` counts towards
        // band `band`.
        const double melLow  = hzToMel (50.0);
        const double melHigh = hzToMel (maxHz);

        melWeights.assign ((size_t) numMelBands, std::vector<float> ((size_t) numBins, 0.0f));

        for (int band = 0; band < numMelBands; ++band)
        {
            const double left   = melToHz (melLow + (melHigh - melLow) * (band)     / (numMelBands + 1));
            const double centre = melToHz (melLow + (melHigh - melLow) * (band + 1) / (numMelBands + 1));
            const double right  = melToHz (melLow + (melHigh - melLow) * (band + 2) / (numMelBands + 1));

            for (int bin = 0; bin <= maxBin; ++bin)
            {
                const double hz = bin * binHz;
                double weight = 0.0;

                if (hz > left && hz <= centre)
                    weight = (hz - left) / (centre - left);
                else if (hz > centre && hz < right)
                    weight = (right - hz) / (right - centre);

                melWeights[(size_t) band][(size_t) bin] = (float) weight;
            }
        }

        // --- DCT table --------------------------------------------------------
        // The Discrete Cosine Transform squeezes the 24 band levels into 13
        // MFCCs. We pre-compute the cosines once.
        dct.assign ((size_t) numMfcc, std::vector<float> ((size_t) numMelBands, 0.0f));

        for (int k = 0; k < numMfcc; ++k)
            for (int band = 0; band < numMelBands; ++band)
                dct[(size_t) k][(size_t) band] =
                    (float) std::cos (pi * k * (band + 0.5) / numMelBands);
    }

    int getWindowSize() const { return windowSamples; }

    // Reads getWindowSize() samples from `window` and writes numFeatures
    // values to `features`. Does not allocate memory.
    void extract (const float* window, float* features)
    {
        std::fill (features, features + numFeatures, 0.0f);

        // --- Remove any constant offset and measure the loudness ------------
        double mean = 0.0;
        for (int i = 0; i < windowSamples; ++i)
            mean += window[i];
        mean /= windowSamples;

        double energy = 0.0;
        for (int i = 0; i < windowSamples; ++i)
        {
            scratch[(size_t) i] = window[i] - mean;
            energy += scratch[(size_t) i] * scratch[(size_t) i];
        }

        const double rms = std::sqrt (energy / windowSamples);
        if (rms < 1.0e-7)
            return; // silence: leave all features at zero

        // --- Time-domain features (measured on the un-faded slice) ----------
        int crossings = 0;
        for (int i = 1; i < windowSamples; ++i)
            if ((scratch[(size_t) i] >= 0.0) != (scratch[(size_t) (i - 1)] >= 0.0))
                ++crossings;

        const double zeroCrossingsPerMs = crossings / (windowSamples / samplesPerMs);

        double firstHalf = 0.0, secondHalf = 0.0;
        const int half = windowSamples / 2;
        for (int i = 0; i < windowSamples; ++i)
            (i < half ? firstHalf : secondHalf) += scratch[(size_t) i] * scratch[(size_t) i];

        const double tailRatio = std::log10 ((secondHalf + 1.0e-12) / (firstHalf + 1.0e-12));

        // --- Spectrum ---------------------------------------------------------
        // Scale to a standard loudness (rms = 1), apply the fade, pad with
        // zeros up to the FFT size, then transform.
        for (int i = 0; i < fftSize; ++i)
        {
            const double value = i < windowSamples
                                     ? scratch[(size_t) i] / rms * fade[(size_t) i]
                                     : 0.0;
            fftData[(size_t) i] = std::complex<double> (value, 0.0);
        }

        fftInPlace (fftData);

        double totalPower = 0.0;
        double weightedHz = 0.0;

        for (int bin = 0; bin <= maxBin; ++bin)
        {
            const double p = std::norm (fftData[(size_t) bin]); // magnitude squared
            power[(size_t) bin] = p;
            totalPower += p;
            weightedHz += p * bin * binHz;
        }

        if (totalPower <= 0.0)
            return;

        // Brightness: the "centre of mass" of the spectrum.
        const double centroidKHz = (weightedHz / totalPower) / 1000.0;

        // Rolloff: walk up the spectrum until we have passed 85% of the energy.
        double running = 0.0;
        double rolloffKHz = maxHz / 1000.0;
        for (int bin = 0; bin <= maxBin; ++bin)
        {
            running += power[(size_t) bin];
            if (running >= 0.85 * totalPower)
            {
                rolloffKHz = bin * binHz / 1000.0;
                break;
            }
        }

        // --- Mel band levels (as a share of the total, then in log form) ----
        double logMel[numMelBands];
        double sumLog = 0.0, sumLinear = 0.0;

        for (int band = 0; band < numMelBands; ++band)
        {
            double bandPower = 0.0;
            const auto& weights = melWeights[(size_t) band];

            for (int bin = 0; bin <= maxBin; ++bin)
                bandPower += weights[(size_t) bin] * power[(size_t) bin];

            const double share = bandPower / totalPower + 1.0e-8;
            logMel[band] = std::log (share);
            sumLog += logMel[band];
            sumLinear += share;
        }

        // Flatness: geometric mean / arithmetic mean of the band levels.
        const double flatness = std::exp (sumLog / numMelBands) / (sumLinear / numMelBands);

        // --- MFCCs ------------------------------------------------------------
        for (int k = 0; k < numMfcc; ++k)
        {
            double sum = 0.0;
            for (int band = 0; band < numMelBands; ++band)
                sum += dct[(size_t) k][(size_t) band] * logMel[band];

            features[k] = (float) (sum / numMelBands);
        }

        features[numMfcc + 0] = (float) centroidKHz;
        features[numMfcc + 1] = (float) rolloffKHz;
        features[numMfcc + 2] = (float) flatness;
        features[numMfcc + 3] = (float) zeroCrossingsPerMs;
        features[numMfcc + 4] = (float) tailRatio;
    }

private:
    // Standard conversions between Hz and the "mel" pitch scale.
    static double hzToMel (double hz)  { return 2595.0 * std::log10 (1.0 + hz / 700.0); }
    static double melToHz (double mel) { return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0); }

    int windowSamples = 0;
    double samplesPerMs = 44.1;

    int fftSize = 0;
    int numBins = 0;
    int maxBin = 0;
    double binHz = 0.0;
    double maxHz = 0.0;

    std::vector<double> fade;
    std::vector<double> scratch;
    std::vector<double> power;
    std::vector<std::complex<double>> fftData;
    std::vector<std::vector<float>> melWeights;
    std::vector<std::vector<float>> dct;
};

} // namespace kushaya
