# Beatbox-to-MIDI Plugin — Phase 2 Brief
> For use with Claude Code

## Project Summary
Phase 1 produced a working Python terminal prototype that listens to the mic, classifies kick/snare/hihat in real time, and prints the result. Phase 2 replaces the terminal print with a real MIDI note sent to a DAW (Studio One), so beatboxing actually triggers a drum sound.

We are still NOT building a plugin yet. Phase 2 is a Python script that sends MIDI over a virtual MIDI bus on macOS.

---

## Developer Context
- Student project — code should be simple, well-commented, and easy to understand
- Developer is a rusty coder — prioritise clarity over cleverness
- All new code should be explained with inline comments
- Ask before using any advanced patterns or abstractions

---

## System Details
- **OS:** macOS with Apple Silicon (M1)
- **Python:** 3.10+ (use python3) — note: in practice we are running Python 3.9.6 and it works fine
- **Microphone:** PreSonus AudioBox USB 96 (set in `INPUT_DEVICE` at the top of `live_classify.py`)
- **DAW:** Studio One
- **Virtual environment:** existing `./venv` — install new packages into it

### Virtual MIDI on macOS
- macOS has a built-in **IAC Driver** ("Inter-Application Communication") that creates a virtual MIDI bus so Python can send notes and Studio One can receive them
- This is a one-time setup in the **Audio MIDI Setup** app — no code involved
- The brief assumes one IAC bus exists named `IAC Driver Bus 1` (the macOS default)

---

## Phase 2 Goals
By the end of Phase 2 we want:

1. A **virtual MIDI bus** set up on macOS (IAC Driver) so Python can send MIDI to Studio One
2. A new script `src/live_to_midi.py` that:
   - Does everything `live_classify.py` does (listen, detect, classify)
   - Sends a MIDI **NoteOn / NoteOff** to the IAC bus for each detected hit
   - Uses **dynamic velocity** — louder beatbox hits send louder MIDI notes
3. **Studio One configured** to receive MIDI from the IAC bus and route it to a drum instrument
4. End-to-end test: beatboxing into the mic plays drum sounds in Studio One in real time

---

## One-Time macOS Setup (manual, no code)

1. Open **Audio MIDI Setup** (search for it in Spotlight)
2. From the menu bar: `Window → Show MIDI Studio`
3. Double-click the **IAC Driver** icon
4. Tick **Device is online**
5. Confirm there is at least one port named `Bus 1` (this is `IAC Driver Bus 1`)
6. Click **Apply** and close

The bus will now appear as a MIDI input in any DAW.

---

## One-Time Studio One Setup (manual, no code)

1. Studio One → Preferences → External Devices → Add → New Instrument
2. Receive From: `IAC Driver Bus 1`
3. Name it `Python Beatbox` (or anything memorable)
4. In your song: add an instrument track, set its input to `Python Beatbox`, and load a drum sampler (e.g. Impact XT) with kick/snare/hihat mapped to General MIDI notes 36/38/42

---

## Scripts to Build

### 1. `src/live_to_midi.py` (new)
**Purpose:** real-time mic → classifier → MIDI out. Mirrors `live_classify.py` but sends MIDI instead of just printing.

Why a new file (not edit `live_classify.py`):
- Keeps the Phase 1 prototype intact as a fallback for debugging
- Easy to compare the two scripts side by side to understand what MIDI adds

**Behaviour:**
- Load the saved model from `models/classifier.pkl`
- Open the AudioBox mic input (same as `live_classify.py`)
- Open the IAC MIDI output port using `mido`
- On detection:
  - Classify the clip
  - Map class → MIDI note (General MIDI drum map):
    - kick → 36 (C1, "Bass Drum 1")
    - snare → 38 (D1, "Acoustic Snare")
    - hihat → 42 (F#1, "Closed Hi-Hat")
  - Convert the trigger RMS into a MIDI velocity (1–127) — see below
  - Send NoteOn, then NoteOff a moment later (drums are one-shots)
  - Print to terminal too: `🥁 KICK   note=36 velocity=92` for debugging
- Allow clean quit with Ctrl+C; close the MIDI port on exit

**Constants near the top of the file (well-commented, easy to tweak):**
- `MIDI_PORT_NAME = "IAC Driver Bus 1"` — change if user named their bus differently
- `NOTE_MAP = {"KICK": 36, "SNARE": 38, "HI-HAT": 42}`
- `NOTE_DURATION = 0.05` — seconds between NoteOn and NoteOff (drums are short)
- `VELOCITY_MIN_RMS` and `VELOCITY_MAX_RMS` — define the RMS range that maps to MIDI velocities 1–127

**Velocity mapping (dynamic):**
- Take the RMS that triggered the detection
- Linearly scale it: `RMS = VELOCITY_MIN_RMS → velocity = 1`, `RMS ≥ VELOCITY_MAX_RMS → velocity = 127`
- Clamp the result to `[1, 127]` (MIDI's valid range — 0 is reserved for "note off")
- Suggested defaults: `VELOCITY_MIN_RMS = 0.02`, `VELOCITY_MAX_RMS = 0.3` — tune after testing

**Libraries to use:** `mido`, `python-rtmidi` (mido backend), plus everything `live_classify.py` already uses.

---

## Libraries to Install

Inside the activated virtual environment:

```bash
pip install mido python-rtmidi
```

Verify it works and finds the IAC bus:
```python
import mido
print(mido.get_output_names())
# Expect to see: 'IAC Driver Bus 1'
```

If `IAC Driver Bus 1` is not listed, the user has not finished the Audio MIDI Setup step above.

---

## What Success Looks Like
1. User opens Audio MIDI Setup, enables IAC Driver, confirms Bus 1 exists
2. User sets up an instrument track in Studio One receiving from `IAC Driver Bus 1`
3. User runs `python src/live_to_midi.py`, beatboxes into the AudioBox mic
4. Drum hits play in Studio One with sensible volume variation — softer beatbox = quieter drum
5. Terminal also prints each hit (label, note number, velocity) for debugging

---

## What We Are NOT Doing in Phase 2
- No plugin format (Phase 3)
- No UI (Phase 3)
- No latency optimisation beyond what Phase 1 already does
- No support for sounds beyond kick, snare, hihat
- No MIDI input from the DAW back to Python
- No learned/curved velocity mapping — keep it linear and tweakable
- No multi-port or multi-channel MIDI routing — one bus, channel 1

---

## Notes for Claude Code
- Add a comment block at the top of `live_to_midi.py` explaining what it does and how to run it (mirror the style of `live_classify.py`)
- Keep new functions small and named clearly (e.g. `rms_to_velocity`, `send_drum_hit`)
- If the IAC port is not found, fail with a helpful message that tells the user to check Audio MIDI Setup
- Always send a matching NoteOff for every NoteOn — never leave a note hanging
- Add a final cleanup that sends NoteOff for any in-flight note on Ctrl+C, just in case
