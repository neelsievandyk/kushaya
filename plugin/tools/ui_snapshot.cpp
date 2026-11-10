// =============================================================================
// ui_snapshot.cpp
//
// Developer tool: draws the plugin window into a .png image without opening
// a DAW. Useful for checking design changes quickly.
//
// Usage:
//   KushayaSnapshot <output.png> [drum]
//
// If [drum] is given (0 = kick, 1 = snare, 2 = hi-hat) the picture shows the
// window as it looks at the moment that drum is hit.
// =============================================================================

#include "../src/PluginEditor.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: KushayaSnapshot <output.png> [drum 0-2]" << std::endl;
        return 2;
    }

    // Starts up the parts of JUCE that drawing needs (fonts, etc).
    juce::ScopedJuceInitialiser_GUI juceGui;

    KushayaProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());

    if (argc > 2)
    {
        // Pretend a drum was just hit and some sound is coming in.
        const int drum = juce::jlimit (0, KushayaProcessor::numDrums - 1, std::atoi (argv[2]));
        processor.hitCounter[drum].fetch_add (1);
        processor.inputLevel.store (0.35f);

        if (auto* kushayaEditor = dynamic_cast<KushayaEditor*> (editor.get()))
            kushayaEditor->refreshFromProcessor();
    }

    // Draw at 2x size so the picture is sharp, like on a Retina screen.
    const juce::Image image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);

    const juce::File outputFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    outputFile.deleteFile();

    juce::FileOutputStream stream (outputFile);
    juce::PNGImageFormat png;

    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cerr << "Could not write " << outputFile.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "Wrote " << outputFile.getFullPathName() << std::endl;
    return 0;
}
