// =============================================================================
// OnsetCapture.h
//
// Step 1 of the pipeline: notice that a beatbox hit has started, then grab a
// very short slice of audio around it so the classifier can look at it.
//
// How it works:
//   - Every incoming sample is fed to pushSample().
//   - Two "envelope followers" track the loudness:
//       fast = reacts instantly, tells us how loud it is right now
//       slow = a lazy average of `fast`, tells us how loud it was recently
//   - A hit starts when the fast envelope is above the threshold AND clearly
//     louder than the slow one. The second test stops the tail of one hit
//     from being mistaken for a new hit.
//   - After a hit starts we wait until `windowMs` of audio has arrived, copy
//     it out of the history buffer, and pushSample() returns true.
//
// Everything is measured in milliseconds and converted to samples in
// prepare(), so it behaves the same at 44.1 kHz, 48 kHz or 96 kHz.
//
// Nothing here allocates memory after prepare(), so it is safe on the
// audio thread.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Fft.h" // for nextPowerOfTwo()

namespace kushaya
{

class OnsetCapture
{
public:
    // How much audio from just BEFORE the trigger point is included in the
    // captured window. The trigger fires slightly after the true start of
    // the sound, so this makes sure the very first attack is not cut off.
    static constexpr float preRollMs = 2.0f;

    // Call once before processing, and again if the sample rate changes.
    void prepare (double newSampleRate, float windowMs)
    {
        sampleRate = newSampleRate;

        windowSamples  = std::max (16, (int) std::lround (windowMs * 0.001 * sampleRate));
        preRollSamples = std::min (windowSamples / 2, (int) std::lround (preRollMs * 0.001 * sampleRate));

        // History buffer. Its size is a power of two so we can wrap around
        // with a cheap bit-mask instead of a modulo.
        const int historySize = nextPowerOfTwo (windowSamples + 16);
        history.assign ((size_t) historySize, 0.0f);
        historyMask = historySize - 1;

        window.assign ((size_t) windowSamples, 0.0f);

        // Envelope follower speeds (see the class comment).
        // 15 ms release is slow enough to bridge the gaps between the
        // cycles of a low kick, so one kick is not seen as several hits.
        fastRelease = (float) std::exp (-1.0 / (0.015 * sampleRate));
        slowCoeff   = (float) (1.0 - std::exp (-1.0 / (0.060 * sampleRate)));

        setRetriggerMs (retriggerMs);
        reset();
    }

    // Forget everything heard so far.
    void reset()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        writePosition = 0;
        fastEnvelope = 0.0f;
        slowEnvelope = 0.0f;
        samplesUntilWindowDone = 0;
        samplesSinceTrigger = retriggerSamples;
        peak = 0.0f;
    }

    // Loudness needed to start a hit, in decibels (0 dB = full scale).
    // Lower (more negative) = more sensitive.
    void setThresholdDb (float db)
    {
        threshold = std::pow (10.0f, db / 20.0f);
    }

    float getThresholdDb() const { return 20.0f * std::log10 (threshold); }

    // Minimum time between two hits.
    void setRetriggerMs (float ms)
    {
        retriggerMs = ms;
        const int wanted = (int) std::lround (ms * 0.001 * sampleRate);

        // Never shorter than one window, otherwise two captures would overlap.
        retriggerSamples = std::max (wanted, windowSamples);
    }

    // Feed one sample. Returns true on the sample where a captured window
    // has just been completed; read it with getWindow().
    bool pushSample (float sample)
    {
        history[(size_t) (writePosition & historyMask)] = sample;

        const float level = std::abs (sample);

        // Fast envelope: jump up instantly, fall back gradually.
        fastEnvelope = std::max (level, fastEnvelope * fastRelease);

        bool windowReady = false;

        if (samplesUntilWindowDone > 0)
        {
            // We are in the middle of capturing a hit.
            if (--samplesUntilWindowDone == 0)
            {
                copyLatestWindow();
                windowReady = true;
            }
        }
        else if (samplesSinceTrigger >= retriggerSamples
                 && fastEnvelope > threshold
                 && fastEnvelope > slowEnvelope * riseRatio)
        {
            // A new hit has started. We already have `preRollSamples` of it
            // in the history buffer; wait for the rest to arrive.
            samplesUntilWindowDone = windowSamples - preRollSamples;
            samplesSinceTrigger = 0;
        }

        // Slow envelope: drifts towards the fast one.
        slowEnvelope += slowCoeff * (fastEnvelope - slowEnvelope);

        // Count up, but stop at the limit so the counter can never overflow
        // during a very long session.
        if (samplesSinceTrigger < retriggerSamples)
            ++samplesSinceTrigger;

        ++writePosition;
        return windowReady;
    }

    // The most recently captured window (valid after pushSample() returned true).
    const float* getWindow() const   { return window.data(); }
    int getWindowSize() const        { return windowSamples; }

    // Loudest sample in that window, 0..1. Used for MIDI velocity.
    float getWindowPeak() const      { return peak; }

    // Delay between the trigger point and the window being ready.
    // This is the plugin's reaction time.
    int getLatencySamples() const    { return windowSamples - preRollSamples; }
    int getPreRollSamples() const    { return preRollSamples; }

private:
    void copyLatestWindow()
    {
        // The newest sample is at writePosition; the window is the
        // `windowSamples` samples ending there.
        const long long first = writePosition - (long long) (windowSamples - 1);
        peak = 0.0f;

        for (int i = 0; i < windowSamples; ++i)
        {
            const float s = history[(size_t) ((first + i) & historyMask)];
            window[(size_t) i] = s;
            peak = std::max (peak, std::abs (s));
        }
    }

    // A new hit must be this many times louder than the recent average.
    // 3.0 = about 10 dB. Lower values made long snares trigger twice.
    static constexpr float riseRatio = 3.0f;

    double sampleRate = 44100.0;

    std::vector<float> history;
    long long historyMask = 0;
    long long writePosition = 0;

    std::vector<float> window;
    int windowSamples = 0;
    int preRollSamples = 0;

    float threshold = 0.03f;
    float retriggerMs = 80.0f;
    int retriggerSamples = 0;

    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;
    float fastRelease = 0.0f;
    float slowCoeff = 0.0f;

    int samplesUntilWindowDone = 0;
    int samplesSinceTrigger = 0;
    float peak = 0.0f;
};

} // namespace kushaya
