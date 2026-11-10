// =============================================================================
// host_test.cpp
//
// Developer tool: a tiny stand-in for a DAW. It loads the finished
// Kushaya.vst3 file, plays .wav recordings through it, and prints the MIDI
// notes the plugin sends back. This tests the real plugin file, not just our
// own code.
//
// Usage:
//   KushayaHostTest <path/to/Kushaya.vst3> <file.wav> [more.wav ...]
//
// The expected drum for each file is guessed from its folder name
// (.../kick/..., .../snare/..., .../hihat/...).
// =============================================================================

#include <iostream>

#include <juce_audio_processors/juce_audio_processors.h>

#include "WavFile.h"

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: KushayaHostTest <Kushaya.vst3> <file.wav>..." << std::endl;
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceGui;

    const double sampleRate = 44100.0;
    const int blockSize = 512;

    // --- Find and load the plugin ---------------------------------------------
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());

    if (found.isEmpty())
    {
        std::cerr << "FAIL: no VST3 plugin found in " << argv[1] << std::endl;
        return 1;
    }

    const auto& description = *found[0];
    std::cout << "Found:  " << description.name << "  by " << description.manufacturerName
              << "  (v" << description.version << ", category: " << description.category << ")" << std::endl;

    juce::String error;
    auto plugin = format.createInstanceFromDescription (description, sampleRate, blockSize, error);

    if (plugin == nullptr)
    {
        std::cerr << "FAIL: could not load the plugin: " << error << std::endl;
        return 1;
    }

    std::cout << "Loaded. Inputs: " << plugin->getTotalNumInputChannels()
              << "  outputs: " << plugin->getTotalNumOutputChannels()
              << "  sends MIDI: " << (plugin->producesMidi() ? "yes" : "NO") << std::endl;

    std::cout << "Parameters:";
    for (auto* parameter : plugin->getParameters())
        std::cout << "  " << parameter->getName (32) << "=" << parameter->getCurrentValueAsText();
    std::cout << std::endl;

    plugin->prepareToPlay (sampleRate, blockSize);

    // --- Play each file through it ---------------------------------------------
    const int numChannels = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numChannels, blockSize);

    int right = 0, wrong = 0, silent = 0, extraNotes = 0;
    bool audioPassedThrough = true;

    for (int arg = 2; arg < argc; ++arg)
    {
        const juce::String path (argv[arg]);

        kushaya::WavData wav;
        if (! kushaya::readWavFile (path.toStdString(), wav))
        {
            std::cerr << "SKIP (cannot read): " << path << std::endl;
            continue;
        }

        // Which note should this file produce? (General MIDI: 36 / 38 / 42)
        const int expectedNote = path.contains ("/kick/") ? 36 : path.contains ("/snare/") ? 38 : 42;

        // Add half a second of silence so the last note-off arrives too.
        wav.samples.resize (wav.samples.size() + (size_t) (sampleRate / 2), 0.0f);

        int loudestNote = -1, loudestVelocity = 0, noteOns = 0, noteOffs = 0;

        for (size_t position = 0; position < wav.samples.size(); position += (size_t) blockSize)
        {
            const int count = (int) juce::jmin ((size_t) blockSize, wav.samples.size() - position);

            buffer.setSize (numChannels, count, false, false, true);
            for (int channel = 0; channel < numChannels; ++channel)
                buffer.copyFrom (channel, 0, wav.samples.data() + position, count);

            juce::MidiBuffer midi;
            plugin->processBlock (buffer, midi);

            // With Voice Thru on (the default) the audio must come out unchanged.
            for (int i = 0; i < count; ++i)
                if (std::abs (buffer.getSample (0, i) - wav.samples[position + (size_t) i]) > 1.0e-6f)
                    audioPassedThrough = false;

            for (const auto event : midi)
            {
                const auto message = event.getMessage();

                if (message.isNoteOn())
                {
                    ++noteOns;
                    if (message.getVelocity() > loudestVelocity)
                    {
                        loudestVelocity = message.getVelocity();
                        loudestNote = message.getNoteNumber();
                    }
                }
                else if (message.isNoteOff())
                {
                    ++noteOffs;
                }
            }
        }

        if (noteOns != noteOffs)
            std::cout << "WARNING: " << noteOns << " note-ons but " << noteOffs << " note-offs in " << path << std::endl;

        if (noteOns == 0)            ++silent;
        else if (loudestNote == expectedNote) ++right;
        else                          ++wrong;

        extraNotes += juce::jmax (0, noteOns - 1);
    }

    std::cout << "Result: " << right << " files gave the right note, " << wrong << " the wrong note, "
              << silent << " gave no note. " << extraNotes << " extra notes from clicks/breaths." << std::endl;
    std::cout << "Audio passed through unchanged: " << (audioPassedThrough ? "yes" : "NO") << std::endl;

    // --- Save the settings and load them back -----------------------------------
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    std::cout << "Settings save/load: ok (" << state.getSize() << " bytes)" << std::endl;

    plugin->releaseResources();
    plugin.reset();

    return (wrong == 0 && silent == 0 && audioPassedThrough) ? 0 : 1;
}
