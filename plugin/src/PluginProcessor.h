// =============================================================================
// PluginProcessor.h
//
// The "brain" of the plugin: the part the DAW calls with audio.
//
// A JUCE plugin is always two classes:
//   - a Processor (this file)  : handles audio and MIDI, runs on the audio thread
//   - an Editor (PluginEditor) : the window you see, runs on the UI thread
//
// The two threads must never wait for each other, so they only talk through
// std::atomic values (single numbers that are safe to read and write from
// two threads at once).
// =============================================================================

#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../core/Engine.h"

class KushayaProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numDrums = 3; // 0 = kick, 1 = snare, 2 = hi-hat

    KushayaProcessor();

    // --- Called by the DAW ---------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "Kushaya"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return true; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    // We have no presets, but the DAW still asks.
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Saving and restoring the knob positions with the DAW project.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Used by the editor --------------------------------------------------

    // All the knobs and switches. The DAW can automate these too.
    juce::AudioProcessorValueTreeState parameters;

    // Converts the 0..100 Sensitivity knob into a trigger level in decibels.
    // 0% = only loud hits (-12 dB), 100% = very quiet sounds too (-48 dB).
    static float sensitivityToThresholdDb (float sensitivityPercent);

    // The editor calls this when you click a pad, to send that drum's note
    // so you can check the routing in your DAW.
    void requestAudition (int drumIndex);

    // Loudest input sample since the editor last looked (0..1).
    std::atomic<float> inputLevel { 0.0f };

    // How many times each drum has been triggered. The editor watches for
    // these numbers going up, and flashes the matching pad.
    std::atomic<int> hitCounter[numDrums] {};
    std::atomic<int> lastVelocity[numDrums] {};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Turns how loud a hit was into a MIDI velocity (1..127).
    int velocityForPeak (float peak, float thresholdDb) const;

    // Adds a note-on to `midi` at sample position `sampleOffset`.
    void startNote (juce::MidiBuffer& midi, int drumIndex, int velocity, int sampleOffset);

    kushaya::Engine engine;

    // Fast, thread-safe pointers to the current parameter values.
    std::atomic<float>* sensitivityValue = nullptr;
    std::atomic<float>* dynamicsValue    = nullptr;
    std::atomic<float>* retriggerValue   = nullptr;
    std::atomic<float>* voiceThruValue   = nullptr;
    std::atomic<float>* noteValue[numDrums] = {};

    // Notes that are currently "held down" and waiting for their note-off.
    int heldNote[numDrums] = { -1, -1, -1 };
    int samplesUntilNoteOff[numDrums] = {};
    int noteLengthSamples = 0;

    std::atomic<bool> auditionRequested[numDrums] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KushayaProcessor)
};
