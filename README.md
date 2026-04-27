# Beatbox to MIDI Plugin

A DAW plugin that listens to you beatboxing and triggers MIDI drum sounds in real time. This repo contains the Phase 1 Python prototype, which proves the core classification pipeline works before any plugin or MIDI code is written.

## What Phase 1 does

1. You record short `.wav` samples of yourself making kick, snare, and hi-hat sounds in your DAW
2. A training script learns to tell them apart using audio features (MFCCs)
3. A live script listens to your mic and prints the detected sound as you beatbox

No UI, no MIDI output yet — just a working terminal prototype.

## Setup

Requires Python 3.10+ and Homebrew on macOS (Apple Silicon).

```bash
# Install PortAudio (needed by sounddevice for mic access)
brew install portaudio

# Create and activate the virtual environment
python3 -m venv venv
source venv/bin/activate

# Install dependencies
pip install sounddevice librosa scikit-learn numpy scipy joblib
```

## Usage

### 1. Add training samples

Record 10–20 short clips (0.5–2 seconds each) of yourself making each sound in Studio One or any DAW. Export as `.wav` and drop them into the correct folders:

```
data/kick/    ← kick drum sounds
data/snare/   ← snare sounds
data/hihat/   ← hi-hat sounds
```

### 2. Train the classifier

```bash
source venv/bin/activate
python src/train_classifier.py
```

Prints accuracy and a per-class breakdown, then saves the model to `models/classifier.pkl`.

### 3. Classify live

```bash
python src/live_classify.py
```

Beatbox into your mic. The terminal prints the detected sound:

```
🥁 Detected: KICK
🥁 Detected: SNARE
🥁 Detected: HI-HAT
```

Press `Ctrl+C` to stop.

## Tuning tips

- **Too many false triggers** — raise `RMS_THRESHOLD` at the top of `live_classify.py`
- **Real hits not detected** — lower `RMS_THRESHOLD`
- **Poor accuracy** — add more training samples (aim for 20+ per class)

## Project structure

```
beatbox-to-midi/
├── data/
│   ├── kick/       ← training samples go here
│   ├── snare/
│   └── hihat/
├── models/         ← classifier.pkl saved here after training
├── src/
│   ├── train_classifier.py
│   └── live_classify.py
└── venv/           ← local Python environment (not committed)
```
