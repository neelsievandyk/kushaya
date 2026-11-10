// =============================================================================
// Engine.h
//
// The whole beatbox-to-drum pipeline in one small class:
//
//     audio in  ->  OnsetCapture  ->  FeatureExtractor  ->  classify()  ->  Hit
//                  (spot the hit)    (describe it)        (name it)
//
// Both the plugin and the command-line test tool use this class, so what you
// test offline is exactly what runs in the DAW.
// =============================================================================

#pragma once

#include "FeatureExtractor.h"
#include "Model.h"
#include "OnsetCapture.h"
#include "RandomForest.h"

namespace kushaya
{

// The features the model was trained on must be the features we compute.
static_assert (model::numFeatures == FeatureExtractor::numFeatures,
               "Model.h does not match FeatureExtractor.h - run scripts/retrain.sh");

struct Hit
{
    int classIndex = 0;      // 0 = kick, 1 = snare, 2 = hi-hat
    float confidence = 0.0f; // 0..1
    float peak = 0.0f;       // loudest sample of the hit, 0..1
};

class Engine
{
public:
    void prepare (double sampleRate)
    {
        // The slice length is fixed by the model: it must be the same length
        // that was used when the model was trained.
        capture.prepare (sampleRate, model::windowMs);
        extractor.prepare (sampleRate, model::windowMs);
    }

    void reset()                          { capture.reset(); }
    void setThresholdDb (float db)        { capture.setThresholdDb (db); }
    void setRetriggerMs (float ms)        { capture.setRetriggerMs (ms); }
    int getLatencySamples() const         { return capture.getLatencySamples(); }

    // Feed one sample. Returns true when a hit has just been recognised,
    // and fills in `hit`.
    bool pushSample (float sample, Hit& hit)
    {
        if (! capture.pushSample (sample))
            return false;

        float features[FeatureExtractor::numFeatures];
        extractor.extract (capture.getWindow(), features);

        const Prediction prediction = classify (features);

        hit.classIndex = prediction.classIndex;
        hit.confidence = prediction.confidence;
        hit.peak       = capture.getWindowPeak();
        return true;
    }

private:
    OnsetCapture capture;
    FeatureExtractor extractor;
};

} // namespace kushaya
