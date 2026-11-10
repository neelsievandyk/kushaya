# =============================================================================
# train_plugin_model.py
#
# Trains the model that lives INSIDE the plugin.
#
# What this script does:
#   1. Builds two small C++ command-line tools from plugin/tools/
#   2. Uses the first tool to turn every .wav in data/kick, data/snare and
#      data/hihat into features - with the plugin's own C++ code, so training
#      and the plugin always agree
#   3. Checks accuracy honestly (testing only on recordings the model has
#      never seen)
#   4. Trains the final Random Forest on everything
#   5. Writes it out as C++ arrays in plugin/core/Model.h
#   6. Uses the second tool to play every recording through the finished
#      plugin engine as a final end-to-end check
#
# You normally don't run this directly. Run this instead, which also
# rebuilds the plugin afterwards:
#
#   ./scripts/retrain.sh
#
# This is separate from src/train_classifier.py, which trains the older
# Python-only prototype (models/classifier.pkl).
# =============================================================================

import collections
import glob
import os
import subprocess
import sys

import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import GroupKFold

# --- Settings you may want to change -------------------------------------------

# Length of the audio slice the plugin looks at for each hit, in milliseconds.
# Shorter = the drum sound follows your voice more tightly.
# Longer  = the model hears more of the sound and may be more accurate.
# The plugin's reaction time is about (WINDOW_MS - 2) milliseconds.
WINDOW_MS = 12

# Loudness (in dB, 0 = full scale) a sound must reach to count as a hit when
# cutting up the training recordings.
TRAIN_THRESHOLD_DB = -30

# Size of the forest. More/deeper trees = slightly more accurate, bigger plugin.
NUM_TREES = 60
MAX_DEPTH = 8
RANDOM_STATE = 42

# Folder name -> (class number, name shown in the plugin).
# The class numbers must stay 0, 1, 2: the plugin's three pads rely on them.
CLASSES = {
    "kick":  (0, "KICK"),
    "snare": (1, "SNARE"),
    "hihat": (2, "HI-HAT"),
}

# --- Paths (relative to the project root) --------------------------------------

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR     = os.path.join(PROJECT_ROOT, "data")
TOOLS_SRC    = os.path.join(PROJECT_ROOT, "plugin", "tools")
MODEL_HEADER = os.path.join(PROJECT_ROOT, "plugin", "core", "Model.h")

# Build output goes in a folder ending in ".nosync" so iCloud Drive leaves it
# alone (it would otherwise try to upload thousands of temporary files).
TOOLS_BIN = os.path.join(PROJECT_ROOT, "build.nosync", "tools")


# --- Step 1: build the C++ tools -----------------------------------------------

def build_tool(name):
    """Compile plugin/tools/<name>.cpp into build.nosync/tools/<name>."""
    os.makedirs(TOOLS_BIN, exist_ok=True)
    source = os.path.join(TOOLS_SRC, name + ".cpp")
    output = os.path.join(TOOLS_BIN, name)

    result = subprocess.run(
        ["clang++", "-std=c++17", "-O2", source, "-o", output],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(result.stderr)
        sys.exit(f"ERROR: could not compile {source}")
    return output


# --- Step 2: extract features --------------------------------------------------

def find_wav_files(folder_name):
    folder = os.path.join(DATA_DIR, folder_name)
    files = glob.glob(os.path.join(folder, "*.wav")) + glob.glob(os.path.join(folder, "*.WAV"))
    return sorted(files)


def load_dataset(extract_tool):
    """
    Run the C++ feature extractor on every recording.

    Returns:
      X      (n_rows, 18) feature rows
      y      (n_rows,)    class number for each row
      files  (n_rows,)    which recording each row came from

    One recording gives several rows (the same hit, sliced a millisecond or
    two earlier/later), which is why we keep track of `files`.
    """
    all_rows, all_labels, all_files = [], [], []

    for folder_name, (class_number, _) in CLASSES.items():
        wav_files = find_wav_files(folder_name)
        if not wav_files:
            print(f"  Warning: no .wav files in data/{folder_name}/")
            continue

        result = subprocess.run(
            [extract_tool, str(WINDOW_MS), str(TRAIN_THRESHOLD_DB), folder_name] + wav_files,
            capture_output=True, text=True,
        )
        if result.stderr.strip():
            print(result.stderr.strip())

        for line in result.stdout.splitlines():
            # Line format:  label,"file path",shiftMs,f0,f1,...,f17
            _, rest = line.split(',"', 1)
            file_path, rest = rest.split('",', 1)
            numbers = rest.split(",")

            all_rows.append([float(v) for v in numbers[1:]])  # skip shiftMs
            all_labels.append(class_number)
            all_files.append(file_path)

    if not all_rows:
        sys.exit("ERROR: no training data found. Put .wav files in data/kick, data/snare, data/hihat.")

    # float32 because that is the precision the plugin computes features in.
    X = np.array(all_rows, dtype=np.float32)
    y = np.array(all_labels)
    files = np.array(all_files)

    print()
    print("Recordings per class:")
    for folder_name, (class_number, _) in CLASSES.items():
        count = len(set(files[y == class_number]))
        print(f"  {folder_name:<6} {count}")
    print(f"  ({len(y)} training rows in total)")

    return X, y, files


# --- Step 3: honest accuracy check ---------------------------------------------

def cross_validate(X, y, files):
    """
    Split the RECORDINGS into 5 groups. Train on 4 groups, test on the 5th,
    and repeat so every recording is tested exactly once.

    Splitting by recording matters: the shifted copies of one hit are nearly
    identical, so if some were in training and some in testing the score
    would look far better than it really is.
    """
    unique_files = np.unique(files)
    if len(unique_files) < 5:
        print("  Too few recordings for an accuracy check - skipping.")
        return

    # Shuffle which recording goes in which group (fixed seed = repeatable).
    shuffled = np.random.RandomState(RANDOM_STATE).permutation(len(unique_files))
    group_of_file = dict(zip(unique_files, shuffled))
    groups = np.array([group_of_file[f] for f in files])

    correct = 0
    confusion = np.zeros((3, 3), dtype=int)  # [true class][predicted class]

    for train_rows, test_rows in GroupKFold(n_splits=5).split(X, y, groups):
        model = RandomForestClassifier(
            n_estimators=NUM_TREES, max_depth=MAX_DEPTH, random_state=RANDOM_STATE)
        model.fit(X[train_rows], y[train_rows])

        predicted = model.predict(X[test_rows])
        correct += int((predicted == y[test_rows]).sum())
        for true_class, predicted_class in zip(y[test_rows], predicted):
            confusion[true_class][predicted_class] += 1

    names = [name for _, name in CLASSES.values()]
    print()
    print(f"Accuracy on unseen recordings: {100.0 * correct / len(y):.1f}%")
    print("Confusion table (rows = what it really was, columns = what the model said):")
    print("          " + "".join(f"{n:>8}" for n in names))
    for i, name in enumerate(names):
        print(f"  {name:<8}" + "".join(f"{v:>8}" for v in confusion[i]))


# --- Step 5: write the model as C++ --------------------------------------------

def format_array(values, per_line=12):
    """Format a list of strings as a neat, wrapped C++ initialiser list."""
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(values[i:i + per_line]))
    return ",\n".join(lines)


def write_model_header(model, path):
    """
    Flatten every tree in the forest into five long arrays, one entry per
    tree node:

      nodeFeature    which feature the node tests   (-1 = this node is a leaf)
      nodeThreshold  the value it compares against
      nodeLeft       node to visit if feature <= threshold
      nodeRight      node to visit otherwise
      nodeValue      at a leaf: the probability of each class
    """
    features, thresholds, lefts, rights, values, roots = [], [], [], [], [], []

    for estimator in model.estimators_:
        tree = estimator.tree_
        offset = len(features)   # where this tree's nodes start in the big arrays
        roots.append(offset)

        for node in range(tree.node_count):
            is_leaf = tree.children_left[node] == -1

            features.append(-1 if is_leaf else int(tree.feature[node]))
            thresholds.append(0.0 if is_leaf else float(tree.threshold[node]))
            lefts.append(0 if is_leaf else offset + int(tree.children_left[node]))
            rights.append(0 if is_leaf else offset + int(tree.children_right[node]))

            # Turn the node's class counts into probabilities that add up to 1.
            counts = tree.value[node][0]
            values.append(counts / counts.sum())

    class_names = [name for _, name in sorted(CLASSES.values())]
    num_classes = len(class_names)

    with open(path, "w") as f:
        f.write("// =============================================================================\n")
        f.write("// Model.h  -  GENERATED FILE, DO NOT EDIT BY HAND\n")
        f.write("//\n")
        f.write("// The trained Random Forest, written out by scripts/train_plugin_model.py.\n")
        f.write("// To change it, add or replace recordings in data/ and run scripts/retrain.sh\n")
        f.write("// =============================================================================\n\n")
        f.write("#pragma once\n\n")
        f.write("namespace kushaya\n{\nnamespace model\n{\n\n")
        f.write("// Length of the audio slice the model was trained on (milliseconds).\n")
        f.write(f"constexpr float windowMs = {float(WINDOW_MS)}f;\n\n")
        f.write(f"constexpr int numFeatures = {model.n_features_in_};\n")
        f.write(f"constexpr int numClasses  = {num_classes};\n")
        f.write(f"constexpr int numTrees    = {len(roots)};\n")
        f.write(f"constexpr int numNodes    = {len(features)};\n\n")

        names = ", ".join(f'"{n}"' for n in class_names)
        f.write(f"inline const char* const classNames[numClasses] = {{ {names} }};\n\n")

        f.write("// Index of each tree's first node.\n")
        f.write("inline const int treeRoot[numTrees] = {\n")
        f.write(format_array([str(v) for v in roots]) + "\n};\n\n")

        f.write("inline const short nodeFeature[numNodes] = {\n")
        f.write(format_array([str(v) for v in features], per_line=20) + "\n};\n\n")

        # repr() prints enough digits to reproduce the number exactly.
        f.write("inline const double nodeThreshold[numNodes] = {\n")
        f.write(format_array([repr(v) for v in thresholds], per_line=5) + "\n};\n\n")

        f.write("inline const int nodeLeft[numNodes] = {\n")
        f.write(format_array([str(v) for v in lefts], per_line=16) + "\n};\n\n")

        f.write("inline const int nodeRight[numNodes] = {\n")
        f.write(format_array([str(v) for v in rights], per_line=16) + "\n};\n\n")

        f.write("inline const float nodeValue[numNodes][numClasses] = {\n")
        rows = ["{ " + ", ".join(f"{p:.6f}f" for p in row) + " }" for row in values]
        f.write(format_array(rows, per_line=3) + "\n};\n\n")

        f.write("} // namespace model\n} // namespace kushaya\n")

    print()
    print(f"Model written to {os.path.relpath(path, PROJECT_ROOT)} "
          f"({len(roots)} trees, {len(features)} nodes)")


# --- Step 6: end-to-end check --------------------------------------------------

def end_to_end_check(evaluate_tool):
    """
    Play every recording through the real plugin engine (C++), at a few
    sensitivity settings, and count how often the loudest hit in the file
    was given the right name.

    This is a check that the C++ side works, not a measure of accuracy:
    the model has already seen these recordings. The honest accuracy figure
    is the "unseen recordings" one printed earlier.
    """
    print()
    print("End-to-end check through the plugin engine:")

    for threshold_db in (-36, -30, -24, -18):
        right = wrong = missed = extra = 0

        for folder_name, (_, class_name) in CLASSES.items():
            wav_files = find_wav_files(folder_name)
            if not wav_files:
                continue

            result = subprocess.run(
                [evaluate_tool, str(threshold_db)] + wav_files,
                capture_output=True, text=True,
            )

            # Group the hits by file. Line format: file, timeMs, class, confidence, peakDb
            hits_per_file = collections.defaultdict(list)
            for line in result.stdout.splitlines():
                file_path, _, found_class, _, peak_db = line.split("\t")
                hits_per_file[file_path].append((float(peak_db), found_class))

            for hits in hits_per_file.values():
                if hits[0][1] == "NONE":
                    missed += 1
                    continue

                # The real hit is the loudest one; anything else in the file
                # is a click or breath that also got through.
                loudest = max(hits)
                if loudest[1] == class_name:
                    right += 1
                else:
                    wrong += 1
                extra += len(hits) - 1

        print(f"  threshold {threshold_db} dB: {right} right, {wrong} wrong, "
              f"{missed} not detected, {extra} extra triggers from clicks/breaths")


# --- Main ----------------------------------------------------------------------

if __name__ == "__main__":
    print("=== Kushaya: training the plugin model ===")

    print()
    print("Step 1: building the feature tool...")
    extract_tool = build_tool("extract_features")

    print("Step 2: extracting features from data/ ...")
    X, y, files = load_dataset(extract_tool)

    print()
    print("Step 3: checking accuracy...")
    cross_validate(X, y, files)

    print()
    print("Step 4: training the final model on all recordings...")
    final_model = RandomForestClassifier(
        n_estimators=NUM_TREES, max_depth=MAX_DEPTH, random_state=RANDOM_STATE)
    final_model.fit(X, y)

    print("Step 5: writing the model as C++...")
    write_model_header(final_model, MODEL_HEADER)

    print()
    print("Step 6: testing the finished engine...")
    evaluate_tool = build_tool("evaluate")
    end_to_end_check(evaluate_tool)

    print()
    print("Done. Rebuild the plugin to use the new model:  ./scripts/build_plugin.sh")
