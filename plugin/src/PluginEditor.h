// =============================================================================
// PluginEditor.h
//
// The plugin window. It is built from four small custom pieces:
//
//   Pad    - one of the three big drum tiles. Flashes when its drum is hit.
//            Click it to send its note; use the arrows to change the note.
//   Knob   - a round dial with its name and value underneath.
//   Meter  - the input level bar, with a blue line showing the trigger level.
//   KushayaLookAndFeel - tells JUCE how to draw dials and switches our way.
//
// KushayaEditor at the bottom puts them all together.
// =============================================================================

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Theme.h"

// -----------------------------------------------------------------------------
// Custom drawing for JUCE's built-in dial and switch.
// -----------------------------------------------------------------------------
class KushayaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

// -----------------------------------------------------------------------------
// A dial connected to one plugin parameter.
// -----------------------------------------------------------------------------
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& parameters,
          const juce::String& parameterId,
          const juce::String& titleToShow,
          const juce::String& unitToShow);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::String title, unit;

    // Keeps the dial and the parameter in sync, in both directions.
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// -----------------------------------------------------------------------------
// One drum tile.
// -----------------------------------------------------------------------------
class Pad final : public juce::Component
{
public:
    Pad (KushayaProcessor& processorToUse, int drumIndexToUse,
         const juce::String& titleToShow, const juce::String& noteParameterId);

    // Light the pad up (called when its drum is triggered).
    void flash();

    // Marks this pad as the most recently hit one (blue outline).
    void setNewest (bool shouldBeNewest);

    // Called ~60 times a second so the flash fades away smoothly.
    void fadeStep();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getNoteBoxBounds() const;
    void changeNoteBy (int amount);
    void drawIcon (juce::Graphics&, juce::Point<float> centre) const;

    KushayaProcessor& processor;
    int drumIndex;
    juce::String title;

    int note = 36;          // the MIDI note this pad currently sends
    float glow = 0.0f;      // 1 = just hit, fades to 0
    bool newest = false;

    // Keeps `note` in sync with the plugin parameter.
    juce::ParameterAttachment noteAttachment;
};

// -----------------------------------------------------------------------------
// The input level bar.
// -----------------------------------------------------------------------------
class Meter final : public juce::Component
{
public:
    void setLevels (float newLevelDb, float newThresholdDb);
    void paint (juce::Graphics&) override;

private:
    float levelDb = -100.0f;
    float thresholdDb = -30.0f;
};

// -----------------------------------------------------------------------------
// The whole window.
// -----------------------------------------------------------------------------
class KushayaEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit KushayaEditor (KushayaProcessor&);
    ~KushayaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Reads the latest hits and input level from the processor and updates
    // the pads and the meter. Normally called by the timer.
    void refreshFromProcessor();

private:
    void timerCallback() override { refreshFromProcessor(); }

    // (The base class already has a member called `processor`, so ours
    // needs a different name.)
    KushayaProcessor& kushaya;
    KushayaLookAndFeel lookAndFeel;

    Pad kickPad, snarePad, hatPad;
    Pad* pads[KushayaProcessor::numDrums] = { &kickPad, &snarePad, &hatPad };
    int hitsSeen[KushayaProcessor::numDrums] = {};

    Knob sensitivityKnob, dynamicsKnob, retriggerKnob;

    Meter meter;
    float meterDb = -100.0f;

    juce::ToggleButton voiceThruButton { "VOICE THRU" };
    juce::AudioProcessorValueTreeState::ButtonAttachment voiceThruAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KushayaEditor)
};
