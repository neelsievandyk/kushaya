// =============================================================================
// PluginProcessor.cpp
//
// See PluginProcessor.h for the overview.
// =============================================================================

#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
// MIDI channel the drum notes are sent on (1..16).
constexpr int midiChannel = 1;

// How long each note is held before its note-off. Drum samplers play the
// whole sample whatever the note length, so this only needs to be short.
constexpr double noteLengthSeconds = 0.06;

// IDs of the three "which note does this drum send" parameters.
const char* const noteParameterIds[KushayaProcessor::numDrums] = { "kickNote", "snareNote", "hatNote" };
}

// =============================================================================
// Setup
// =============================================================================

KushayaProcessor::KushayaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "KushayaState", createParameterLayout())
{
    sensitivityValue = parameters.getRawParameterValue ("sensitivity");
    dynamicsValue    = parameters.getRawParameterValue ("dynamics");
    retriggerValue   = parameters.getRawParameterValue ("retrigger");
    voiceThruValue   = parameters.getRawParameterValue ("voiceThru");

    for (int drum = 0; drum < numDrums; ++drum)
        noteValue[drum] = parameters.getRawParameterValue (noteParameterIds[drum]);
}

juce::AudioProcessorValueTreeState::ParameterLayout KushayaProcessor::createParameterLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    using IntParam   = juce::AudioParameterInt;
    using BoolParam  = juce::AudioParameterBool;
    using Range      = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The number after each ID is a "version hint" JUCE asks for; leave it at 1.

    // How easily a sound triggers a drum.
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "sensitivity", 1 }, "Sensitivity",
                                              Range (0.0f, 100.0f, 1.0f), 50.0f,
                                              juce::AudioParameterFloatAttributes().withLabel ("%")));

    // How much the loudness of your voice changes the MIDI velocity.
    // 0% = every hit is the same loudness, 100% = fully follows your voice.
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "dynamics", 1 }, "Dynamics",
                                              Range (0.0f, 100.0f, 1.0f), 60.0f,
                                              juce::AudioParameterFloatAttributes().withLabel ("%")));

    // Shortest allowed gap between two hits, in milliseconds.
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "retrigger", 1 }, "Retrigger",
                                              Range (30.0f, 250.0f, 1.0f), 80.0f,
                                              juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Which MIDI note each drum sends. Defaults are the General MIDI drum map.
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "kickNote", 1 },  "Kick Note",  0, 127, 36));
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "snareNote", 1 }, "Snare Note", 0, 127, 38));
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "hatNote", 1 },   "Hi-Hat Note", 0, 127, 42));

    // On = your voice is passed through to the output. Off = output is silent.
    layout.add (std::make_unique<BoolParam> (juce::ParameterID { "voiceThru", 1 }, "Voice Thru", true));

    return layout;
}

bool KushayaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // We work on mono or stereo tracks, with the same shape in and out.
    const auto input  = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    const bool monoOrStereo = input == juce::AudioChannelSet::mono()
                              || input == juce::AudioChannelSet::stereo();

    return monoOrStereo && input == output;
}

void KushayaProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    engine.prepare (sampleRate);
    noteLengthSamples = (int) (noteLengthSeconds * sampleRate);

    for (int drum = 0; drum < numDrums; ++drum)
    {
        heldNote[drum] = -1;
        samplesUntilNoteOff[drum] = 0;
    }
}

// =============================================================================
// Helpers
// =============================================================================

float KushayaProcessor::sensitivityToThresholdDb (float sensitivityPercent)
{
    return juce::jmap (sensitivityPercent, 0.0f, 100.0f, -12.0f, -48.0f);
}

void KushayaProcessor::requestAudition (int drumIndex)
{
    if (drumIndex >= 0 && drumIndex < numDrums)
        auditionRequested[drumIndex].store (true);
}

int KushayaProcessor::velocityForPeak (float peak, float thresholdDb) const
{
    // Where does this hit sit between "only just loud enough to trigger"
    // (0.0) and "nearly full scale" (1.0)?
    const float peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    const float loudness = juce::jlimit (0.0f, 1.0f, juce::jmap (peakDb, thresholdDb, -3.0f, 0.0f, 1.0f));

    // A fully dynamic velocity runs from 25 (soft) to 127 (hard).
    const float dynamicVelocity = 25.0f + loudness * 102.0f;

    // The Dynamics knob blends between a fixed velocity of 100 and that.
    const float amount = dynamicsValue->load() / 100.0f;
    const float velocity = 100.0f + amount * (dynamicVelocity - 100.0f);

    return juce::jlimit (1, 127, juce::roundToInt (velocity));
}

void KushayaProcessor::startNote (juce::MidiBuffer& midi, int drumIndex, int velocity, int sampleOffset)
{
    // If this drum's previous note is still held, release it first so the
    // DAW never ends up with a stuck note.
    if (heldNote[drumIndex] >= 0)
        midi.addEvent (juce::MidiMessage::noteOff (midiChannel, heldNote[drumIndex]), sampleOffset);

    const int note = juce::jlimit (0, 127, juce::roundToInt (noteValue[drumIndex]->load()));

    midi.addEvent (juce::MidiMessage::noteOn (midiChannel, note, (juce::uint8) velocity), sampleOffset);

    heldNote[drumIndex] = note;
    samplesUntilNoteOff[drumIndex] = noteLengthSamples;

    // Tell the editor, so it can flash the pad.
    lastVelocity[drumIndex].store (velocity);
    hitCounter[drumIndex].fetch_add (1);
}

// =============================================================================
// The audio callback. The DAW calls this hundreds of times a second with a
// small block of audio. It must be fast and must never allocate memory,
// lock, or touch the UI.
// =============================================================================

void KushayaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numInputs   = getTotalNumInputChannels();
    const int numOutputs  = getTotalNumOutputChannels();

    // We create MIDI; we don't pass on any that came in.
    midi.clear();

    // Pick up the current knob positions.
    const float thresholdDb = sensitivityToThresholdDb (sensitivityValue->load());
    engine.setThresholdDb (thresholdDb);
    engine.setRetriggerMs (retriggerValue->load());

    // A pad was clicked in the editor: send its note straight away.
    for (int drum = 0; drum < numDrums; ++drum)
        if (auditionRequested[drum].exchange (false))
            startNote (midi, drum, 100, 0);

    const float* left  = numInputs > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* right = numInputs > 1 ? buffer.getReadPointer (1) : nullptr;

    float blockPeak = 0.0f;
    kushaya::Hit hit;

    for (int i = 0; i < numSamples; ++i)
    {
        // Mix the input down to one channel for analysis.
        float sample = 0.0f;
        if (left != nullptr)
            sample = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];

        blockPeak = juce::jmax (blockPeak, std::abs (sample));

        // Send any note-offs that are due on this sample.
        for (int drum = 0; drum < numDrums; ++drum)
        {
            if (heldNote[drum] >= 0 && --samplesUntilNoteOff[drum] <= 0)
            {
                midi.addEvent (juce::MidiMessage::noteOff (midiChannel, heldNote[drum]), i);
                heldNote[drum] = -1;
            }
        }

        // The core of the plugin: did a beatbox hit just get recognised?
        if (engine.pushSample (sample, hit))
            startNote (midi, hit.classIndex, velocityForPeak (hit.peak, thresholdDb), i);
    }

    // Keep the loudest level seen until the editor collects it.
    if (blockPeak > inputLevel.load())
        inputLevel.store (blockPeak);

    // Voice Thru off: silence the audio (the MIDI still goes out).
    if (voiceThruValue->load() < 0.5f)
        buffer.clear();

    // Any output channels that have no matching input must not contain junk.
    for (int channel = numInputs; channel < numOutputs; ++channel)
        buffer.clear (channel, 0, numSamples);
}

// =============================================================================
// Saving / loading settings
// =============================================================================

void KushayaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KushayaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KushayaProcessor::createEditor()
{
    return new KushayaEditor (*this);
}

// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KushayaProcessor();
}
