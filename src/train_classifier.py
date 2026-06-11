# =============================================================================
# train_classifier.py
#
# What this script does:
#   1. Loads .wav files from data/kick/, data/snare/, and data/hihat/
#   2. Extracts audio features (MFCCs) from each file
#   3. Trains a Random Forest classifier on those features
#   4. Prints accuracy on a held-out test set
#   5. Saves the trained model to models/classifier.pkl
#
# How to run (from the project root):
#   source venv/bin/activate
#   python src/train_classifier.py
#
# Before running: drop .wav files into data/kick/, data/snare/, data/hihat/
# =============================================================================

import os
import sys
import glob
import collections

import numpy as np
import librosa
import joblib
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, classification_report

# --- Constants ----------------------------------------------------------------

# Paths are relative to the project root (where you run this script from).
DATA_DIR    = "data"
MODEL_PATH  = "models/classifier.pkl"

# Number of MFCC (Mel-frequency cepstral coefficient) features to extract.
# MFCCs capture the "shape" of a sound — great for telling kick vs snare apart.
# 13 is a standard starting point for audio classification.
N_MFCC = 13

# The sample rate to use when loading audio. 22050 Hz is the librosa default.
# Using a fixed rate ensures all files are compared fairly, even if they were
# recorded at different rates in your DAW.
SAMPLE_RATE = 22050

# 20% of samples go to testing; 80% are used for training.
TEST_SIZE    = 0.20
RANDOM_STATE = 42  # Fixed seed so results are the same every run.

# Maps folder names to integer class labels.
FOLDER_TO_LABEL = {
    "kick":  0,
    "snare": 1,
    "hihat": 2,
}

# Human-readable names for each label (used in printed output).
LABEL_NAMES = {0: "kick", 1: "snare", 2: "hihat"}


# --- Feature Extraction -------------------------------------------------------

def extract_features(file_path):
    """
    Load a .wav file and return a 1-D feature vector of length 15:
      - 13 MFCC means              (overall spectral shape)
      -  1 zero-crossing rate mean (noise-like vs tonal — separates hihat from snare)
      -  1 spectral centroid mean  ("brightness" in Hz — hihats are brighter)

    Why these three together:
      MFCCs alone tend to confuse snare and hihat because averaging across
      time hides their different *characters*. ZCR and centroid expose that
      character directly: a hihat is noisy and bright; a snare is less so.

    Returns None if the file can't be read, so the caller can skip it.
    """
    try:
        # Load audio, resampling to a consistent 22050 Hz.
        audio, sr = librosa.load(file_path, sr=SAMPLE_RATE)
        return compute_feature_vector(audio, sr)

    except Exception as e:
        print(f"  Warning: could not read '{file_path}': {e}")
        return None


def compute_feature_vector(audio, sr):
    """
    Build the 15-element feature vector from in-memory audio.
    Shared by training (loaded from file) and live (captured from mic).
    """
    # 13 MFCC means — overall spectral shape.
    mfccs = librosa.feature.mfcc(y=audio, sr=sr, n_mfcc=N_MFCC)
    mfcc_mean = np.mean(mfccs, axis=1)

    # Zero-crossing rate (per frame) → take the mean.
    # High for noisy sounds (hihat), lower for tonal sounds (snare body).
    zcr = librosa.feature.zero_crossing_rate(y=audio)
    zcr_mean = float(np.mean(zcr))

    # Spectral centroid (per frame) → take the mean. Units: Hz.
    # Roughly the "center of mass" of the spectrum. Hihats sit much higher.
    centroid = librosa.feature.spectral_centroid(y=audio, sr=sr)
    centroid_mean = float(np.mean(centroid))

    # Concatenate everything into one fixed-length vector of shape (15,).
    return np.concatenate([mfcc_mean, [zcr_mean, centroid_mean]])


# --- Dataset Loading ----------------------------------------------------------

def load_dataset(data_dir):
    """
    Walk through data/kick/, data/snare/, data/hihat/ and extract features
    from every .wav file found. Returns (X, y) as numpy arrays.

    X shape: (n_samples, 15) — one row per audio clip (13 MFCC + ZCR + centroid)
    y shape: (n_samples,)    — integer class label per clip
    """
    all_features = []
    all_labels   = []

    for folder_name, label_int in FOLDER_TO_LABEL.items():
        folder_path = os.path.join(data_dir, folder_name)

        # Find all .wav files (case-insensitive: both .wav and .WAV)
        wav_files = glob.glob(os.path.join(folder_path, "*.wav")) + \
                    glob.glob(os.path.join(folder_path, "*.WAV"))

        if not wav_files:
            print(f"  Warning: no .wav files found in {folder_path}/")
            print(f"           → Add some recordings and run this script again.")
            continue

        print(f"  Loading {len(wav_files)} file(s) from {folder_path}/")

        for wav_path in wav_files:
            features = extract_features(wav_path)
            if features is not None:
                all_features.append(features)
                all_labels.append(label_int)

    # Fail with a helpful message if nothing was loaded at all.
    if len(all_features) == 0:
        print()
        print("ERROR: No audio samples were loaded.")
        print("Expected layout:")
        print("  data/kick/   ← put kick .wav files here")
        print("  data/snare/  ← put snare .wav files here")
        print("  data/hihat/  ← put hi-hat .wav files here")
        print()
        print("Record short clips in your DAW and export as .wav,")
        print("then drop them into the correct folders and re-run.")
        sys.exit(1)

    X = np.array(all_features)
    y = np.array(all_labels)

    # Print a summary of how many samples were loaded per class.
    counts = collections.Counter(y)
    print()
    print("Samples per class:")
    for label_int, label_name in LABEL_NAMES.items():
        print(f"  {label_name:<8} {counts.get(label_int, 0)} samples")
    print(f"  Total    {len(y)} samples")
    print()

    return X, y


# --- Training -----------------------------------------------------------------

def train_and_evaluate(X, y):
    """
    Split data into train/test, train a Random Forest, and print accuracy.
    Returns the fitted model.
    """
    # Split: 80% for training, 20% for testing.
    # stratify=y means each class is split proportionally — so a rare class
    # isn't accidentally left out of the test set.
    try:
        X_train, X_test, y_train, y_test = train_test_split(
            X, y,
            test_size=TEST_SIZE,
            random_state=RANDOM_STATE,
            stratify=y,
        )
    except ValueError:
        # stratify fails when any class has fewer than 2 samples.
        print("  Warning: at least one class has very few samples; "
              "skipping stratified split.")
        X_train, X_test, y_train, y_test = train_test_split(
            X, y,
            test_size=TEST_SIZE,
            random_state=RANDOM_STATE,
        )

    # Train a Random Forest.
    # 100 trees gives a good balance of accuracy and training speed.
    # Each tree learns slightly different patterns; the majority vote wins.
    print("Training Random Forest classifier...")
    model = RandomForestClassifier(n_estimators=100, random_state=RANDOM_STATE)
    model.fit(X_train, y_train)

    # Evaluate on the held-out test set.
    y_pred   = model.predict(X_test)
    accuracy = accuracy_score(y_test, y_pred)
    print(f"Test accuracy: {accuracy * 100:.1f}%")
    print()

    # classification_report shows per-class precision and recall — handy for
    # spotting if one sound type is being confused with another.
    target_names = [LABEL_NAMES[i] for i in sorted(LABEL_NAMES)]
    print("Per-class breakdown:")
    print(classification_report(y_test, y_pred, target_names=target_names))

    return model


# --- Saving -------------------------------------------------------------------

def save_model(model, path):
    """Serialize the trained model to disk using joblib."""
    # Make sure the models/ directory exists (in case it was deleted).
    os.makedirs(os.path.dirname(path), exist_ok=True)

    joblib.dump(model, path)
    print(f"Model saved to {path}")


# --- Main ---------------------------------------------------------------------

if __name__ == "__main__":
    print("=== Beatbox-to-MIDI: Training Classifier ===")
    print()

    print("Step 1: Loading dataset...")
    X, y = load_dataset(DATA_DIR)

    print("Step 2: Training model...")
    model = train_and_evaluate(X, y)

    print("Step 3: Saving model...")
    save_model(model, MODEL_PATH)

    print()
    print("Training complete!")
    print("Next step: run  python src/live_classify.py  to classify in real time.")
