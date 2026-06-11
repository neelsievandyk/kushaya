# =============================================================================
# live_to_midi.py
#
# What this script does:
#   1. Loads the trained classifier from models/classifier.pkl
#   2. Opens your microphone (PreSonus AudioBox) using sounddevice
#   3. Monitors incoming audio in real time
#   4. When a loud-enough sound is detected, captures ~1 second of audio
#   5. Classifies the sound (kick / snare / hi-hat)
#   6. Sends a MIDI NoteOn (then NoteOff a moment later) to a virtual MIDI bus
#      so Studio One can hear it and play a drum sample
#   7. Velocity is DYNAMIC — louder beatbox = louder MIDI note
#
# How to run (from the project root):
#   source venv/bin/activate
#   python src/live_to_midi.py
#
# Press Ctrl+C to stop.
#
# Prerequisites:
#   - Phase 1 model exists at models/classifier.pkl
#   - PortAudio installed: brew install portaudio
#   - macOS IAC Driver enabled with a port named "Bus 1"
#     (Audio MIDI Setup → Window → Show MIDI Studio → double-click IAC Driver
#      → tick "Device is online")
#   - Studio One configured with an instrument track receiving from
#     "IAC Driver Bus 1" and Impact XT (or another drum sampler) loaded
# =============================================================================

import os
import sys
import time
import collections

import numpy as np
import librosa
import joblib
import sounddevice as sd
import mido

# --- Constants ----------------------------------------------------------------

# Path to the saved model (relative to the project root).
MODEL_PATH = "models/classifier.pkl"

# --- Audio settings (must match train_classifier.py) ---
SAMPLE_RATE = 22050
BLOCK_SIZE = 512         # Smaller = lower polling latency. ~23ms at 22050 Hz.
RMS_THRESHOLD = 0.06     # Loudness to trigger a detection. Raise = less sensitive.
                         # Note: 0.02 catches the decay of previous hits as phantom
                         # triggers. 0.06 sits above typical noise floor / decay
                         # but still below a real hihat (~0.07+).
CAPTURE_SECONDS = 0.3    # Audio length sent to classifier. Short = low latency AND
                         # avoids the previous hit overlapping the capture window
                         # when beatboxing fast.
COOLDOWN_SECONDS = 0.1   # Minimum gap between two detections. Drop further if you
                         # want to play even faster, but watch for double-triggers.
N_MFCC = 13

# --- Mic input device (same as live_classify.py) ---
# None = system default. String = match by name (substring works too).
INPUT_DEVICE = "AudioBox USB 96"

# --- MIDI settings ---

# Name of the virtual MIDI output port. macOS default is "IAC Driver Bus 1".
# To list available ports, run:
#   python -c "import mido; print(mido.get_output_names())"
MIDI_PORT_NAME = "IAC Driver Bus 1"

# MIDI channel to send on (1–16 in Studio One, but mido uses 0–15 internally).
# Channel 1 in the DAW = channel 0 in mido.
MIDI_CHANNEL = 0

# Class → MIDI note number (General MIDI drum map).
# Impact XT factory kits map these to the right sounds automatically.
NOTE_MAP = {
    "KICK":   36,  # C1  — Bass Drum 1
    "SNARE":  38,  # D1  — Acoustic Snare
    "HI-HAT": 42,  # F#1 — Closed Hi-Hat
}

# How long (in seconds) between NoteOn and NoteOff for each hit.
# Drums are one-shots — the sampler plays the full sample regardless of
# note length — but we always send a matching NoteOff for cleanliness.
NOTE_DURATION = 0.05

# Velocity behaviour. While tuning, fixed velocity makes the rhythm feel
# consistent. Flip USE_DYNAMIC_VELOCITY back to True once accuracy is solid.
USE_DYNAMIC_VELOCITY = False
FIXED_VELOCITY = 100     # Used when USE_DYNAMIC_VELOCITY is False (1–127).

# Used only when USE_DYNAMIC_VELOCITY is True. A hit at VELOCITY_MIN_RMS
# becomes velocity 1; a hit at VELOCITY_MAX_RMS (or louder) becomes 127.
VELOCITY_MIN_RMS = 0.02
VELOCITY_MAX_RMS = 0.30

# Human-readable output labels, indexed by the model's integer predictions.
LABEL_NAMES = {0: "KICK", 1: "SNARE", 2: "HI-HAT"}


# --- Model Loading ------------------------------------------------------------

def load_model(path):
    """Load the trained classifier. Exit with a helpful message if missing."""
    if not os.path.exists(path):
        print(f"ERROR: Model file not found at '{path}'")
        print("Run train_classifier.py first to create the model:")
        print("  python src/train_classifier.py")
        sys.exit(1)

    model = joblib.load(path)
    print(f"Model loaded from {path}")
    return model


# --- MIDI Helpers -------------------------------------------------------------

def open_midi_port(port_name):
    """
    Open the named MIDI output port. Exit with a helpful message if not found.
    """
    available = mido.get_output_names()
    if port_name not in available:
        print(f"ERROR: MIDI port '{port_name}' not found.")
        print("Available ports:")
        for name in available:
            print(f"  - {name}")
        print()
        print("Fix: open 'Audio MIDI Setup', show MIDI Studio, double-click")
        print("IAC Driver, tick 'Device is online', confirm a port named 'Bus 1'.")
        sys.exit(1)

    port = mido.open_output(port_name)
    print(f"MIDI output opened on '{port_name}'")
    return port


def rms_to_velocity(rms):
    """
    Convert an RMS loudness value (~0.02–0.5) into a MIDI velocity (1–127).
    Linear scaling between VELOCITY_MIN_RMS and VELOCITY_MAX_RMS, clamped to
    MIDI's valid range. Velocity 0 is reserved for "note off" so we never use it.
    """
    span = VELOCITY_MAX_RMS - VELOCITY_MIN_RMS
    fraction = (rms - VELOCITY_MIN_RMS) / span
    velocity = int(fraction * 127)

    # Clamp into the valid MIDI velocity range.
    velocity = max(1, min(127, velocity))
    return velocity


# --- Audio Helpers ------------------------------------------------------------

def compute_rms(audio_block):
    """RMS loudness of an audio block. 0.0 = silence."""
    return float(np.sqrt(np.mean(audio_block ** 2)))


def extract_features(audio_clip):
    """
    Build the 15-element feature vector from an in-memory audio clip.
    Must match train_classifier.compute_feature_vector() exactly:
      - 13 MFCC means
      -  1 zero-crossing rate mean (separates noisy hihat from tonal snare)
      -  1 spectral centroid mean  (brightness — higher for hihats)
    """
    mfccs = librosa.feature.mfcc(y=audio_clip, sr=SAMPLE_RATE, n_mfcc=N_MFCC)
    mfcc_mean = np.mean(mfccs, axis=1)

    zcr = librosa.feature.zero_crossing_rate(y=audio_clip)
    zcr_mean = float(np.mean(zcr))

    centroid = librosa.feature.spectral_centroid(y=audio_clip, sr=SAMPLE_RATE)
    centroid_mean = float(np.mean(centroid))

    return np.concatenate([mfcc_mean, [zcr_mean, centroid_mean]])


def classify_clip(model, audio_clip):
    """Return the predicted label string for an audio clip."""
    features = extract_features(audio_clip).reshape(1, -1)
    prediction_int = model.predict(features)[0]
    return LABEL_NAMES[prediction_int]


# --- Live Classification + MIDI Loop ------------------------------------------

def run_live_to_midi(model, midi_port):
    """
    Open the mic, classify hits in real time, and send MIDI to the DAW.

    Note scheduling:
    - We don't sleep between NoteOn and NoteOff (that would block the main
      loop and miss new hits). Instead, we keep a list of "pending NoteOffs"
      with their scheduled send time, and the main loop pops and sends any
      that are due each tick.
    """

    audio_buffer = collections.deque(maxlen=int(SAMPLE_RATE * 3))
    last_trigger_time = [0.0]

    # Pending NoteOffs: list of (send_time, note) tuples.
    pending_note_offs = []

    def audio_callback(indata, frames, time_info, status):
        if status:
            print(f"  Audio warning: {status}")
        audio_buffer.extend(indata[:, 0])

    print()
    print("Warming up classifier (first MFCC call compiles numba — takes ~5s)...")
    extract_features(np.zeros(SAMPLE_RATE, dtype=np.float32))

    print("Ready! Listening for sounds... (Press Ctrl+C to stop)")
    print(f"Input device: {INPUT_DEVICE}")
    print(f"MIDI port:    {MIDI_PORT_NAME}")
    print(f"RMS threshold: {RMS_THRESHOLD}")
    print()

    try:
        with sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=1,
            dtype="float32",
            blocksize=BLOCK_SIZE,
            callback=audio_callback,
            device=INPUT_DEVICE,
        ):
            while True:
                time.sleep(BLOCK_SIZE / SAMPLE_RATE)
                now = time.time()

                # Send any NoteOffs whose scheduled time has passed.
                # Loop runs at ~46ms so NoteOff jitter is fine for drums.
                while pending_note_offs and pending_note_offs[0][0] <= now:
                    _, note = pending_note_offs.pop(0)
                    midi_port.send(mido.Message(
                        "note_off", note=note, velocity=0, channel=MIDI_CHANNEL
                    ))

                if len(audio_buffer) < BLOCK_SIZE:
                    continue

                # Measure current loudness from the most recent block.
                recent_block = np.array(list(audio_buffer)[-BLOCK_SIZE:])
                rms = compute_rms(recent_block)

                cooldown_elapsed = (now - last_trigger_time[0]) > COOLDOWN_SECONDS

                if rms > RMS_THRESHOLD and cooldown_elapsed:
                    last_trigger_time[0] = now

                    # Grab the last CAPTURE_SECONDS of audio for classification.
                    n_samples = int(SAMPLE_RATE * CAPTURE_SECONDS)
                    clip = np.array(list(audio_buffer)[-n_samples:], dtype=np.float32)
                    if len(clip) < n_samples:
                        clip = np.pad(clip, (n_samples - len(clip), 0))

                    label = classify_clip(model, clip)
                    note = NOTE_MAP[label]
                    velocity = rms_to_velocity(rms) if USE_DYNAMIC_VELOCITY else FIXED_VELOCITY

                    midi_port.send(mido.Message(
                        "note_on", note=note, velocity=velocity, channel=MIDI_CHANNEL
                    ))
                    pending_note_offs.append((now + NOTE_DURATION, note))

                    # Pad the label so the columns line up nicely.
                    print(f"🥁 {label:<7} note={note}  velocity={velocity:<3}  rms={rms:.3f}")

    except KeyboardInterrupt:
        print()
        print("Stopping...")

    finally:
        # Make sure no notes are left hanging in the DAW.
        for _, note in pending_note_offs:
            midi_port.send(mido.Message(
                "note_off", note=note, velocity=0, channel=MIDI_CHANNEL
            ))
        midi_port.close()
        print("MIDI port closed. Goodbye!")


# --- Main ---------------------------------------------------------------------

if __name__ == "__main__":
    print("=== Beatbox-to-MIDI: Live → MIDI ===")
    model = load_model(MODEL_PATH)
    midi_port = open_midi_port(MIDI_PORT_NAME)
    run_live_to_midi(model, midi_port)
