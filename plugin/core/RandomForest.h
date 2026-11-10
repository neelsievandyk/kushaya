// =============================================================================
// RandomForest.h
//
// Step 3 of the pipeline: decide whether the features describe a kick, a
// snare or a hi-hat.
//
// The model is a "random forest": a collection of decision trees. Each tree
// asks a chain of yes/no questions ("is feature 13 <= 2.4?") until it reaches
// a leaf, and the leaf says how likely each class is. The forest's answer is
// the average of all its trees.
//
// The trees are trained in Python (scripts/train_plugin_model.py) and written
// out as plain arrays in Model.h. This file just walks those arrays, so the
// plugin needs no Python and no machine-learning library.
// =============================================================================

#pragma once

#include "Model.h"

namespace kushaya
{

struct Prediction
{
    int classIndex = 0;      // 0 = kick, 1 = snare, 2 = hi-hat
    float confidence = 0.0f; // 0..1, share of the vote the winner received
};

// `features` must point to model::numFeatures values.
inline Prediction classify (const float* features)
{
    float votes[model::numClasses] = {};

    for (int tree = 0; tree < model::numTrees; ++tree)
    {
        // Start at the tree's root and follow the questions down to a leaf.
        int node = model::treeRoot[tree];

        while (model::nodeFeature[node] >= 0)
        {
            const bool goLeft = (double) features[model::nodeFeature[node]] <= model::nodeThreshold[node];
            node = goLeft ? model::nodeLeft[node] : model::nodeRight[node];
        }

        // A leaf: add its class probabilities to the running totals.
        for (int c = 0; c < model::numClasses; ++c)
            votes[c] += model::nodeValue[node][c];
    }

    Prediction result;
    float total = 0.0f;

    for (int c = 0; c < model::numClasses; ++c)
    {
        total += votes[c];
        if (votes[c] > votes[result.classIndex])
            result.classIndex = c;
    }

    result.confidence = total > 0.0f ? votes[result.classIndex] / total : 0.0f;
    return result;
}

} // namespace kushaya
