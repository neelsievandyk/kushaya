// =============================================================================
// PluginEditor.cpp
//
// See PluginEditor.h for the overview. All sizes are in pixels.
// =============================================================================

#include "PluginEditor.h"

namespace
{
// --- Window layout -----------------------------------------------------------
constexpr int windowWidth  = 640;
constexpr int windowHeight = 404;
constexpr int margin       = 28;   // empty space around the edge

constexpr int padsTop      = 78;
constexpr int padsHeight   = 172;
constexpr int padGap       = 16;

constexpr int controlsTop    = 270;
constexpr int controlsHeight = 106;
constexpr int knobWidth      = 92;

constexpr float cornerRadius = 20.0f;

// The meter shows levels from this value up to 0 dB.
constexpr float meterFloorDb = -60.0f;

// Note names. Studio One calls MIDI note 36 "C1", so we do too.
juce::String noteName (int note)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
}
}

// =============================================================================
// KushayaLookAndFeel
// =============================================================================

void KushayaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float position, float startAngle, float endAngle,
                                           juce::Slider&)
{
    const auto area   = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centre = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 4.0f;
    const float angle  = startAngle + position * (endAngle - startAngle);

    const juce::PathStrokeType stroke (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // The full arc, in a quiet colour.
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::track);
    g.strokePath (backgroundArc, stroke);

    // The part of the arc up to the current value, in orange.
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (theme::orange);
    g.strokePath (valueArc, stroke);

    // The dial face.
    const float faceRadius = radius - 9.0f;
    g.setColour (theme::card);
    g.fillEllipse (juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre));
    g.setColour (theme::outline);
    g.drawEllipse (juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre), 1.0f);

    // A blue dot on the face showing where the dial points.
    const float dotDistance = faceRadius - 8.0f;
    const juce::Point<float> dot (centre.x + dotDistance * std::sin (angle),
                                  centre.y - dotDistance * std::cos (angle));
    g.setColour (theme::blue);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
}

void KushayaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool /*highlighted*/, bool /*down*/)
{
    const bool on = button.getToggleState();
    const auto area = button.getLocalBounds().toFloat();

    // A pill-shaped switch on the left...
    const auto pill = juce::Rectangle<float> (36.0f, 20.0f)
                          .withPosition (area.getX(), area.getCentreY() - 10.0f);
    g.setColour (on ? theme::blue : theme::track);
    g.fillRoundedRectangle (pill, 10.0f);

    const float thumbX = on ? pill.getRight() - 18.0f : pill.getX() + 2.0f;
    g.setColour (theme::card);
    g.fillEllipse (thumbX, pill.getY() + 2.0f, 16.0f, 16.0f);

    // ...and its label on the right.
    g.setColour (on ? theme::ink : theme::muted);
    g.setFont (theme::font (11.0f, "Demi Bold", 0.12f));
    g.drawText (button.getButtonText(), area.withTrimmedLeft (pill.getWidth() + 10.0f),
                juce::Justification::centredLeft);
}

// =============================================================================
// Knob
// =============================================================================

Knob::Knob (juce::AudioProcessorValueTreeState& parameters, const juce::String& parameterId,
            const juce::String& titleToShow, const juce::String& unitToShow)
    : title (titleToShow),
      unit (unitToShow),
      attachment (parameters, parameterId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // The dial sweeps from "7 o'clock" round to "5 o'clock".
    const float pi = juce::MathConstants<float>::pi;
    slider.setRotaryParameters (1.25f * pi, 2.75f * pi, true);

    // Redraw the number underneath whenever the dial moves.
    slider.onValueChange = [this] { repaint(); };

    addAndMakeVisible (slider);
}

void Knob::resized()
{
    // The dial takes the top; the bottom 38 pixels are for the two text lines.
    slider.setBounds (getLocalBounds().withTrimmedBottom (38));
}

void Knob::paint (juce::Graphics& g)
{
    auto textArea = getLocalBounds().removeFromBottom (36);

    g.setColour (theme::ink);
    g.setFont (theme::font (14.0f, "Demi Bold"));
    g.drawText (juce::String (juce::roundToInt (slider.getValue())) + unit,
                textArea.removeFromTop (18), juce::Justification::centred);

    g.setColour (theme::muted);
    g.setFont (theme::font (10.0f, "Demi Bold", 0.14f));
    g.drawText (title.toUpperCase(), textArea, juce::Justification::centred);
}

// =============================================================================
// Pad
// =============================================================================

Pad::Pad (KushayaProcessor& processorToUse, int drumIndexToUse,
          const juce::String& titleToShow, const juce::String& noteParameterId)
    : processor (processorToUse),
      drumIndex (drumIndexToUse),
      title (titleToShow),
      noteAttachment (*processorToUse.parameters.getParameter (noteParameterId),
                      [this] (float newValue)
                      {
                          note = juce::roundToInt (newValue);
                          repaint();
                      })
{
    noteAttachment.sendInitialUpdate();
}

void Pad::flash()
{
    glow = 1.0f;
    repaint();
}

void Pad::setNewest (bool shouldBeNewest)
{
    if (newest != shouldBeNewest)
    {
        newest = shouldBeNewest;
        repaint();
    }
}

void Pad::fadeStep()
{
    if (glow > 0.0f)
    {
        glow *= 0.9f;
        if (glow < 0.01f)
            glow = 0.0f;

        repaint();
    }
}

juce::Rectangle<float> Pad::getNoteBoxBounds() const
{
    return getLocalBounds().toFloat().reduced (14.0f).removeFromBottom (32.0f);
}

void Pad::changeNoteBy (int amount)
{
    const int newNote = juce::jlimit (0, 127, note + amount);
    if (newNote != note)
        noteAttachment.setValueAsCompleteGesture ((float) newNote);
}

void Pad::mouseDown (const juce::MouseEvent& event)
{
    const auto noteBox = getNoteBoxBounds();

    if (noteBox.contains (event.position))
    {
        // Left third = note down, right third = note up.
        if (event.position.x < noteBox.getX() + noteBox.getWidth() / 3.0f)
            changeNoteBy (-1);
        else if (event.position.x > noteBox.getRight() - noteBox.getWidth() / 3.0f)
            changeNoteBy (+1);

        return;
    }

    // Anywhere else on the pad: play this drum's note.
    // (The flash happens automatically, when the processor reports the hit.)
    processor.requestAudition (drumIndex);
}

void Pad::drawIcon (juce::Graphics& g, juce::Point<float> centre) const
{
    // Three deliberately simple shapes:  kick = solid disc,
    // snare = ring,  hi-hat = two bars (a pair of cymbals seen side-on).
    g.setColour (theme::ink);

    if (drumIndex == 0)
    {
        g.fillEllipse (juce::Rectangle<float> (28.0f, 28.0f).withCentre (centre));
    }
    else if (drumIndex == 1)
    {
        g.drawEllipse (juce::Rectangle<float> (25.0f, 25.0f).withCentre (centre), 3.0f);
    }
    else
    {
        g.fillRoundedRectangle (juce::Rectangle<float> (32.0f, 3.5f).withCentre (centre.translated (0.0f, -4.5f)), 1.75f);
        g.fillRoundedRectangle (juce::Rectangle<float> (32.0f, 3.5f).withCentre (centre.translated (0.0f, 4.5f)), 1.75f);
    }
}

void Pad::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.5f);

    // Background: off-white normally, orange while glowing.
    g.setColour (theme::card.interpolatedWith (theme::orange, glow));
    g.fillRoundedRectangle (area, cornerRadius);

    // Outline: blue if this was the last pad hit.
    g.setColour (newest ? theme::blue : theme::outline);
    g.drawRoundedRectangle (area, cornerRadius, newest ? 2.0f : 1.0f);

    drawIcon (g, { area.getCentreX(), area.getY() + 50.0f });

    g.setColour (theme::ink);
    g.setFont (theme::font (15.0f, "Demi Bold", 0.16f));
    g.drawText (title, area.withY (area.getY() + 80.0f).withHeight (24.0f), juce::Justification::centred);

    // --- Note box:  <   C1 · 36   > ---------------------------------------------
    const auto noteBox = getNoteBoxBounds();

    g.setColour (theme::background.interpolatedWith (theme::card, glow * 0.6f));
    g.fillRoundedRectangle (noteBox, noteBox.getHeight() * 0.5f);

    g.setColour (theme::ink);
    g.setFont (theme::font (13.0f, "Demi Bold"));
    // The note name and number with a small centred dot between them.
    // (The dot is not a plain-ASCII character, so it is written as UTF-8 bytes.)
    const auto separator = juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    g.drawText (noteName (note) + separator + juce::String (note), noteBox, juce::Justification::centred);

    // The two arrows, drawn as small "v" shapes turned on their side.
    g.setColour (theme::muted);
    const juce::PathStrokeType arrowStroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const float midY = noteBox.getCentreY();

    juce::Path leftArrow;
    leftArrow.startNewSubPath (noteBox.getX() + 19.0f, midY - 5.0f);
    leftArrow.lineTo (noteBox.getX() + 14.0f, midY);
    leftArrow.lineTo (noteBox.getX() + 19.0f, midY + 5.0f);
    g.strokePath (leftArrow, arrowStroke);

    juce::Path rightArrow;
    rightArrow.startNewSubPath (noteBox.getRight() - 19.0f, midY - 5.0f);
    rightArrow.lineTo (noteBox.getRight() - 14.0f, midY);
    rightArrow.lineTo (noteBox.getRight() - 19.0f, midY + 5.0f);
    g.strokePath (rightArrow, arrowStroke);
}

// =============================================================================
// Meter
// =============================================================================

void Meter::setLevels (float newLevelDb, float newThresholdDb)
{
    if (! juce::approximatelyEqual (newLevelDb, levelDb)
        || ! juce::approximatelyEqual (newThresholdDb, thresholdDb))
    {
        levelDb = newLevelDb;
        thresholdDb = newThresholdDb;
        repaint();
    }
}

void Meter::paint (juce::Graphics& g)
{
    // The bar itself is 8 pixels tall, centred in our area, and inset a
    // little so the threshold marker can stick out above and below.
    const auto bar = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth() - 4.0f, 8.0f);

    // Converts a decibel value into an x position along the bar.
    auto xForDb = [&bar] (float db)
    {
        const float proportion = juce::jlimit (0.0f, 1.0f, juce::jmap (db, meterFloorDb, 0.0f, 0.0f, 1.0f));
        return bar.getX() + proportion * bar.getWidth();
    };

    g.setColour (theme::track);
    g.fillRoundedRectangle (bar, 4.0f);

    // The level: pale while below the trigger level, full orange above it.
    const float levelX = xForDb (levelDb);
    if (levelX > bar.getX() + 1.0f)
    {
        g.setColour (levelDb >= thresholdDb ? theme::orange : theme::orangeSoft);
        g.fillRoundedRectangle (bar.withRight (juce::jmax (levelX, bar.getX() + 8.0f)), 4.0f);
    }

    // The trigger level, as a blue line.
    g.setColour (theme::blue);
    g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 20.0f)
                                .withCentre ({ xForDb (thresholdDb), bar.getCentreY() }), 1.5f);
}

// =============================================================================
// KushayaEditor
// =============================================================================

KushayaEditor::KushayaEditor (KushayaProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse),
      kushaya (processorToUse),
      kickPad  (processorToUse, 0, "KICK",   "kickNote"),
      snarePad (processorToUse, 1, "SNARE",  "snareNote"),
      hatPad   (processorToUse, 2, "HI-HAT", "hatNote"),
      sensitivityKnob (processorToUse.parameters, "sensitivity", "Sensitivity", "%"),
      dynamicsKnob    (processorToUse.parameters, "dynamics",    "Dynamics",    "%"),
      retriggerKnob   (processorToUse.parameters, "retrigger",   "Retrigger",   " ms"),
      voiceThruAttachment (processorToUse.parameters, "voiceThru", voiceThruButton)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* pad : pads)
        addAndMakeVisible (pad);

    addAndMakeVisible (sensitivityKnob);
    addAndMakeVisible (dynamicsKnob);
    addAndMakeVisible (retriggerKnob);
    addAndMakeVisible (meter);
    addAndMakeVisible (voiceThruButton);

    // Don't flash for hits that happened before the window was opened.
    for (int drum = 0; drum < KushayaProcessor::numDrums; ++drum)
        hitsSeen[drum] = kushaya.hitCounter[drum].load();

    setSize (windowWidth, windowHeight);
    setResizable (false, false);

    // Refresh the pads and the meter 60 times a second.
    startTimerHz (60);
}

KushayaEditor::~KushayaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KushayaEditor::refreshFromProcessor()
{
    // --- Pads -------------------------------------------------------------------
    for (int drum = 0; drum < KushayaProcessor::numDrums; ++drum)
    {
        const int hits = kushaya.hitCounter[drum].load();

        if (hits != hitsSeen[drum])
        {
            hitsSeen[drum] = hits;
            pads[drum]->flash();

            for (int other = 0; other < KushayaProcessor::numDrums; ++other)
                pads[other]->setNewest (other == drum);
        }

        pads[drum]->fadeStep();
    }

    // --- Meter ------------------------------------------------------------------
    // Take the loudest level since last time (and reset it), then let the
    // bar fall back gently rather than flickering.
    const float level = kushaya.inputLevel.exchange (0.0f);
    const float levelDb = juce::Decibels::gainToDecibels (level, -100.0f);
    meterDb = juce::jmax (levelDb, meterDb - 1.5f);

    const float sensitivity = kushaya.parameters.getRawParameterValue ("sensitivity")->load();
    meter.setLevels (meterDb, KushayaProcessor::sensitivityToThresholdDb (sensitivity));
}

void KushayaEditor::resized()
{
    // --- Three pads across the middle -------------------------------------------
    const int padWidth = (windowWidth - 2 * margin - 2 * padGap) / 3;

    for (int drum = 0; drum < KushayaProcessor::numDrums; ++drum)
        pads[drum]->setBounds (margin + drum * (padWidth + padGap), padsTop, padWidth, padsHeight);

    // --- Three knobs bottom-left ------------------------------------------------
    sensitivityKnob.setBounds (margin,                 controlsTop, knobWidth, controlsHeight);
    dynamicsKnob.setBounds    (margin + knobWidth,     controlsTop, knobWidth, controlsHeight);
    retriggerKnob.setBounds   (margin + 2 * knobWidth, controlsTop, knobWidth, controlsHeight);

    // --- Input panel bottom-right (the panel itself is drawn in paint()) --------
    auto panel = juce::Rectangle<int> (margin + 3 * knobWidth + 20, controlsTop,
                                       windowWidth - 2 * margin - 3 * knobWidth - 20, controlsHeight)
                     .reduced (18, 0);

    meter.setBounds (panel.getX(), controlsTop + 34, panel.getWidth(), 22);
    voiceThruButton.setBounds (panel.getX(), controlsTop + 68, panel.getWidth(), 24);
}

void KushayaEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // --- Header: logo mark, name, tagline ---------------------------------------
    g.setColour (theme::orange);
    g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ margin + 10.0f, 39.0f }));
    g.setColour (theme::blue);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ margin + 19.0f, 30.0f }));

    g.setColour (theme::ink);
    g.setFont (theme::font (25.0f, "Bold"));
    g.drawText ("kushaya", margin + 32, 22, 200, 34, juce::Justification::centredLeft);

    g.setColour (theme::muted);
    g.setFont (theme::font (10.0f, "Demi Bold", 0.16f));
    g.drawText ("BEATBOX TO MIDI", windowWidth - margin - 200, 22, 200, 34, juce::Justification::centredRight);

    // --- Input panel ------------------------------------------------------------
    const auto panel = juce::Rectangle<int> (margin + 3 * knobWidth + 20, controlsTop,
                                             windowWidth - 2 * margin - 3 * knobWidth - 20, controlsHeight)
                           .toFloat();

    g.setColour (theme::card);
    g.fillRoundedRectangle (panel, cornerRadius);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (panel.reduced (0.5f), cornerRadius, 1.0f);

    g.setColour (theme::muted);
    g.setFont (theme::font (10.0f, "Demi Bold", 0.14f));
    g.drawText ("INPUT", panel.reduced (18.0f, 0.0f).withY (panel.getY() + 12.0f).withHeight (18.0f),
                juce::Justification::centredLeft);
}
