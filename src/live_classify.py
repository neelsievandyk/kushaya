# =============================================================================
# live_classify.py
#
# What this script does:
#   1. Loads the trained classifier from models/classifier.pkl
#   2. Opens your microphone using sounddevice
#   3. Monitors incoming audio in real time
#   4. When a loud-enough sound is detected, captures ~1 second of audio
#   5. Extracts the same MFCC features used during training
#   6. Classifies the sound and prints the result, e.g.: 🥁 Detected: KICK
#
# How to run (from the project root):
#   source venv/bin/activate
#   python src/live_classify.py
#
# Press Ctrl+C to stop.
#
# Prerequisites:
#   - Run train_classifier.py first to create models/classifier.pkl
#   - PortAudio must be installed: brew install portaudio
#   - macOS: grant Terminal microphone access in
#     System Preferences → Privacy & Security → Microphone
# =============================================================================

import os
import sys
import time
import collections

import numpy as np
import librosa
import joblib
import sounddevice as sd

# --- Constants ----------------------------------------------------------------

# Path to the saved model (relative to the project root).
MODEL_PATH = "models/classifier.pkl"

# Audio settings — MUST match what train_classifier.py used.
# If you get crackling audio, try SAMPLE_RATE = 44100 here AND in training.
#
# M1 Mac note: M1 Macs often default to 44100 or 48000 Hz internally.
# PortAudio handles resampling to 22050 Hz automatically, so this usually
# works fine without any changes.
SAMPLE_RATE = 22050

# How many samples to read per audio callback.
# 1024 samples ≈ 46ms at 22050 Hz. Smaller = more responsive; larger = less CPU.
BLOCK_SIZE = 1024

# Volume threshold for triggering a detection.
# RMS (root mean square) measures loudness: 0.0 = silence, ~0.02–0.5 = a real hit.
# Raise this number if random room noise triggers false detections.
# Lower it if real sounds are being missed.
RMS_THRESHOLD = 0.02

# How many seconds of audio to capture and classify after a trigger.
CAPTURE_SECONDS = 1.0

# Minimum time (seconds) between two consecutive detections.
# Prevents the same hit from being classified multiple times.
COOLDOWN_SECONDS = 0.3

# Number of MFCCs — must match train_classifier.py.
N_MFCC = 13

# Which input device to record from.
# Set to None to use the system default microphone.
# Set to a string (e.g. "AudioBox USB 96") to target a specific interface —
# sounddevice does a substring match, so partial names work too ("AudioBox").
# To list available devices, run:
#   python -c "import sounddevice as sd; print(sd.query_devices())"
INPUT_DEVICE = "AudioBox USB 96"

# Human-readable output labels, indexed by the model's integer predictions.
LABEL_NAMES = {0: "KICK", 1: "SNARE", 2: "HI-HAT"}


# --- Model Loading ------------------------------------------------------------

def load_model(path):
    """
    Load the trained classifier from disk.
    Exits with a helpful message if the file doesn't exist yet.
    """
    if not os.path.exists(path):
        print(f"ERROR: Model file not found at '{path}'")
        print("Run train_classifier.py first to create the model:")
        print("  python src/train_classifier.py")
        sys.exit(1)

    model = joblib.load(path)
    print(f"Model loaded from {path}")
    return model


# --- Audio Helpers ------------------------------------------------------------

def compute_rms(audio_block):
    """
    Compute the Root Mean Square (RMS) of an audio block.
    RMS is a reliable measure of loudness: 0.0 = silence, higher = louder.
    """
    return float(np.sqrt(np.mean(audio_block ** 2)))


def extract_features(audio_clip):
    """
    Extract 13 MFCC mean values from an in-memory audio clip.
    This is the same logic as in train_classifier.py, but operates on
    a numpy array rather than a file path (no file I/O needed here).
    """
    # Compute MFCCs: shape is (N_MFCC, time_frames)
    mfccs = librosa.feature.mfcc(y=audio_clip, sr=SAMPLE_RATE, n_mfcc=N_MFCC)

    # Average across time → shape (N_MFCC,) = (13,)
    mfcc_mean = np.mean(mfccs, axis=1)

    return mfcc_mean


def classify_clip(model, audio_clip):
    """
    Extract features from a clip and return the predicted label string.
    """
    features = extract_features(audio_clip)

    # sklearn's predict() expects a 2-D array: (n_samples, n_features).
    # We have one sample, so we reshape (13,) → (1, 13).
    features_2d = features.reshape(1, -1)

    prediction_int = model.predict(features_2d)[0]
    return LABEL_NAMES[prediction_int]


# --- Live Classification Loop -------------------------------------------------

def run_live_classifier(model):
    """
    Open the microphone and classify sounds in real time.

    Design overview:
    - sounddevice calls audio_callback() every BLOCK_SIZE samples.
    - audio_callback() appends each block to a rolling deque (circular buffer).
    - The main loop checks the most recent block's RMS every ~46ms.
    - When RMS exceeds RMS_THRESHOLD (and cooldown has elapsed), it grabs
      the last CAPTURE_SECONDS of audio, classifies it, and prints the result.
    """

    # Rolling buffer holding the last 3 seconds of audio.
    # maxlen automatically discards old samples — no manual trimming needed.
    audio_buffer = collections.deque(maxlen=int(SAMPLE_RATE * 3))

    # last_trigger_time is stored in a list (not a plain variable) so that
    # the audio_callback closure can modify it. Plain Python closures can
    # read outer variables but cannot reassign them without this trick.
    last_trigger_time = [0.0]

    def audio_callback(indata, frames, time_info, status):
        """Called by sounddevice every BLOCK_SIZE samples."""
        if status:
            # status messages indicate driver-level issues (e.g. buffer overflow).
            print(f"  Audio warning: {status}")

        # indata shape: (frames, channels). Take channel 0 for mono.
        mono = indata[:, 0]
        audio_buffer.extend(mono)

    print()
    print("Warming up classifier (first MFCC call compiles numba — takes ~5s)...")

    # Trigger one dummy MFCC call so numba JIT-compiles before we go live.
    # Without this, the first real detection would have a multi-second delay.
    dummy = np.zeros(SAMPLE_RATE, dtype=np.float32)
    extract_features(dummy)

    print("Ready! Listening for sounds... (Press Ctrl+C to stop)")
    print(f"RMS threshold: {RMS_THRESHOLD} — adjust if needed at the top of live_classify.py")
    print()

    # Open the microphone stream.
    #
    # M1 Mac note: if sounddevice raises an OSError about PortAudio,
    # make sure you've run:  brew install portaudio
    #
    # macOS permission note: if the mic records only silence, go to
    # System Preferences → Privacy & Security → Microphone and enable Terminal.
    # Show which mic we're actually using so it's obvious if it's wrong.
    if INPUT_DEVICE is None:
        print(f"Input device: system default")
    else:
        print(f"Input device: {INPUT_DEVICE}")

    try:
        with sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=1,           # mono — we only need one channel
            dtype="float32",
            blocksize=BLOCK_SIZE,
            callback=audio_callback,
            device=INPUT_DEVICE,  # None = system default; string = match by name
        ):
            while True:
                # Sleep for one block duration so we don't spin the CPU.
                time.sleep(BLOCK_SIZE / SAMPLE_RATE)

                # We need at least one block's worth of data before we can check RMS.
                if len(audio_buffer) < BLOCK_SIZE:
                    continue

                # Look at the most recent block to measure the current volume.
                recent_block = np.array(list(audio_buffer)[-BLOCK_SIZE:])
                rms = compute_rms(recent_block)

                now = time.time()
                cooldown_elapsed = (now - last_trigger_time[0]) > COOLDOWN_SECONDS

                if rms > RMS_THRESHOLD and cooldown_elapsed:
                    # Record the trigger time immediately to start the cooldown.
                    last_trigger_time[0] = now

                    # Grab the last CAPTURE_SECONDS of audio for classification.
                    n_samples = int(SAMPLE_RATE * CAPTURE_SECONDS)
                    clip = np.array(list(audio_buffer)[-n_samples:], dtype=np.float32)

                    # If the buffer doesn't have a full second yet (e.g. right at startup),
                    # pad the front with silence so the clip length is consistent.
                    if len(clip) < n_samples:
                        clip = np.pad(clip, (n_samples - len(clip), 0))

                    label = classify_clip(model, clip)
                    print(f"🥁 Detected: {label}")

    except KeyboardInterrupt:
        # Ctrl+C — exit gracefully.
        print()
        print("Stopped. Goodbye!")


# --- Main ---------------------------------------------------------------------

if __name__ == "__main__":
    # M1/macOS note: sounddevice requires PortAudio.
    # If you see: OSError: PortAudio library not found
    # Run: brew install portaudio
    print("=== Beatbox-to-MIDI: Live Classifier ===")

    model = load_model(MODEL_PATH)
    run_live_classifier(model)
