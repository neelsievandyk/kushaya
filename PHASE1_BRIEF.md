# Beatbox-to-MIDI Plugin — Phase 1 Brief
> For use with Claude Code

## Project Summary
We are building a DAW plugin that listens to a user beatboxing into a microphone, classifies the sounds in real-time, and triggers MIDI notes mapped to drum sounds. This is Phase 1: a working Python prototype that proves the core classification pipeline works.

---

## Developer Context
- Student project — code should be simple, well-commented, and easy to understand
- Developer is a rusty coder — prioritise clarity over cleverness
- All code should be explained with inline comments
- Ask before using any advanced patterns or abstractions

---

## System Details
- **OS:** macOS with Apple Silicon (M1)
- **Python:** 3.10+ (use python3)
- **Microphone:** PreSonus M7 condenser mic (via USB audio interface or direct)
- **Virtual environment:** already created at `./venv` — install all packages into it

### M1-Specific Notes
- Do NOT use `pip install librosa` without first ensuring `llvmlite` and `numba` are M1-compatible
- Recommended: install via `pip install librosa` with Python 3.10+ — this should work on M1 but verify
- If audio device issues arise, use `sounddevice` and check input device index explicitly
- Avoid any libraries that require Rosetta or x86 emulation

---

## Phase 1 Goals
By the end of Phase 1 we want:

1. A script that **extracts audio features** from pre-recorded .wav samples
2. A script that **trains a simple classifier** (kick, snare, hi-hat) on those features
3. A script that **listens live** to the mic, detects a beatbox sound, classifies it, and prints the result to the terminal

**Note on samples:** Training samples are recorded externally by the user in their DAW (Studio One), exported as .wav files, and dropped manually into the correct `data/` subfolders. No sample recording script is needed.

We are NOT building a plugin yet. No UI. No MIDI output yet. Just a working terminal-based prototype.

---

## Folder Structure to Create
```
beatbox-to-midi/
├── README.md
├── PHASE1_BRIEF.md
├── venv/
├── data/
│   ├── kick/        ← user drops WAV samples here from DAW
│   ├── snare/       ← user drops WAV samples here from DAW
│   └── hihat/       ← user drops WAV samples here from DAW
├── models/          ← trained classifier saved here
└── src/
    ├── train_classifier.py
    └── live_classify.py
```

---

## Scripts to Build

### 1. `src/train_classifier.py`
**Purpose:** loads all the saved .wav samples, extracts features, trains a classifier, and saves the model

**Behaviour:**
- Loop through all files in `data/kick/`, `data/snare/`, `data/hihat/`
- For each file, extract **MFCC features** (Mel-frequency cepstral coefficients) using `librosa`
  - Use 13 MFCCs, take the mean across time — this gives a simple fixed-length feature vector per sample
- Label each sample with its class (kick=0, snare=1, hihat=2)
- Train a **Random Forest classifier** using `scikit-learn`
- Print the accuracy on a small test split so we can see how well it works
- Save the trained model to `models/classifier.pkl` using `joblib`
- Print a summary of how many samples were used per class

**Libraries to use:** `librosa`, `scikit-learn`, `numpy`, `joblib`

---

### 2. `src/live_classify.py`
**Purpose:** listens to the mic in real-time and classifies beatbox sounds as they happen

**Behaviour:**
- Load the saved model from `models/classifier.pkl`
- Open the mic input stream using `sounddevice`
- Continuously monitor the audio level
- When audio exceeds a volume threshold (onset detection), capture a short clip (~1 second)
- Extract the same MFCC features as used in training
- Run the classifier and print the result to the terminal, e.g.:
  ```
  🥁 Detected: KICK
  🥁 Detected: HIHAT
  🥁 Detected: SNARE
  ```
- Add a short cooldown between detections (~0.3 seconds) to avoid double-triggering
- Allow the user to quit cleanly with Ctrl+C

**Libraries to use:** `sounddevice`, `librosa`, `numpy`, `joblib`

---

## Libraries to Install
Run these inside the activated virtual environment:

```bash
pip install sounddevice librosa scikit-learn numpy scipy joblib
```

Verify M1 compatibility after install by running:
```python
import librosa
import sounddevice
print("All good!")
```

---

## What Success Looks Like
- User records ~30 samples each of kick, snare, hihat in their DAW and drops them into the correct `data/` folders
- User runs `train_classifier.py` and sees accuracy above 80%
- User runs `live_classify.py`, beatboxes into the mic, and sees correct labels printed in the terminal most of the time

---

## What We Are NOT Doing in Phase 1
- No MIDI output (Phase 2)
- No plugin format (Phase 3)
- No UI (Phase 3)
- No real-time latency optimisation yet
- No support for sounds beyond kick, snare, hihat

---

## Notes for Claude Code
- Add a comment block at the top of each file explaining what it does
- Use `if __name__ == "__main__":` pattern for all scripts
- Keep functions small and named clearly
- If something is complex, add a plain-English comment explaining why
- If you are unsure about M1 compatibility for any library, flag it with a comment
