#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// On-screen 88-key piano that is also played from the computer keyboard.
//
//   Lower octave:  Z S X D C V G B H N J M  , L . ; /
//   Upper octave:  Q 2 W 3 E R 5 T 6 Y 7 U  I 9 O 0 P [ = ]
//   Space = sustain pedal, Left/Right = octave, Up/Down = velocity
class PianoKeyboard : public juce::MidiKeyboardComponent
{
public:
    std::function<void (bool down)> onSustain;
    std::function<void (int delta)> onOctave;
    std::function<void (int delta)> onVelocity;

    explicit PianoKeyboard (juce::MidiKeyboardState& s)
        : MidiKeyboardComponent (s, horizontalKeyboard)
    {
        clearKeyMappings();

        const char* lower = "zsxdcvgbhnjm,l.;/";
        const char* upper = "q2w3er5t6y7ui9o0p[=]";

        for (int i = 0; lower[i] != 0; ++i) addMapping (lower[i], i);
        for (int i = 0; upper[i] != 0; ++i) addMapping (upper[i], 12 + i);

        setAvailableRange (21, 108);
        setScrollButtonsVisible (false);
        setWantsKeyboardFocus (true);
        setKeyPressBaseOctave (baseOctave);
        setOctaveForMiddleC (4);

        setColour (keyDownOverlayColourId,  juce::Colour (0xff3fa7ff).withAlpha (0.85f));
        setColour (mouseOverKeyOverlayColourId, juce::Colour (0xff3fa7ff).withAlpha (0.25f));
        setColour (shadowColourId, juce::Colours::black.withAlpha (0.35f));
    }

    int getBaseOctave() const { return baseOctave; }

    void setBaseOctave (int o)
    {
        // Release any computer-keyboard notes first, otherwise their note-offs
        // would be sent to the new octave and the old notes would hang.
        MidiKeyboardComponent::focusLost (focusChangedDirectly);

        baseOctave = juce::jlimit (1, 6, o);
        setKeyPressBaseOctave (baseOctave);
        repaint();
    }

    int lowestMappedNote() const  { return baseOctave * 12; }
    int highestMappedNote() const { return baseOctave * 12 + 31; }

    void resized() override
    {
        MidiKeyboardComponent::resized();
        setKeyWidth ((float) getWidth() / 52.0f);   // 52 white keys on an 88-key piano
        setLowestVisibleKey (21);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::leftKey)  { if (onOctave)   onOctave (-1);   return true; }
        if (k == juce::KeyPress::rightKey) { if (onOctave)   onOctave (1);    return true; }
        if (k == juce::KeyPress::upKey)    { if (onVelocity) onVelocity (1);  return true; }
        if (k == juce::KeyPress::downKey)  { if (onVelocity) onVelocity (-1); return true; }
        if (k == juce::KeyPress::spaceKey) return true;

        return MidiKeyboardComponent::keyPressed (k);
    }

    bool keyStateChanged (bool isKeyDown) override
    {
        const bool spaceDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey);

        if (spaceDown != sustainHeld)
        {
            sustainHeld = spaceDown;
            if (onSustain) onSustain (spaceDown);
        }

        return MidiKeyboardComponent::keyStateChanged (isKeyDown) || spaceDown;
    }

    void focusLost (FocusChangeType t) override
    {
        if (sustainHeld)
        {
            sustainHeld = false;
            if (onSustain) onSustain (false);
        }
        MidiKeyboardComponent::focusLost (t);
        repaint();
    }

    void focusGained (FocusChangeType) override { repaint(); }

protected:
    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override
    {
        MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
        drawRangeMark (note, g, area);

        if (const auto label = labelFor (note); label.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff1d4f80));
            g.setFont (juce::FontOptions (juce::jmin (13.0f, area.getWidth() * 0.8f), juce::Font::bold));
            g.drawText (label, area.withTrimmedTop (area.getHeight() * 0.55f).withHeight (16.0f),
                        juce::Justification::centred, false);
        }
    }

    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour fill) override
    {
        MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, fill);

        if (const auto label = labelFor (note); label.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::jmin (12.0f, area.getWidth() * 0.9f));
            g.drawText (label, area.removeFromBottom (area.getHeight() * 0.3f), juce::Justification::centred);
        }
    }

private:
    void addMapping (char c, int offset)
    {
        setKeyPressForNote (juce::KeyPress (c), offset);
        labels[offset] = juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) c));
    }

    juce::String labelFor (int note) const
    {
        if (! hasKeyboardFocus (false))
            return {};

        const int offset = note - baseOctave * 12;
        if (offset < 0 || offset > 31)
            return {};

        // Where both rows reach the same note, show the upper-row key.
        return labels[offset];
    }

    void drawRangeMark (int note, juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (note >= lowestMappedNote() && note <= highestMappedNote())
        {
            g.setColour (juce::Colour (0xff3fa7ff).withAlpha (hasKeyboardFocus (false) ? 0.9f : 0.35f));
            g.fillRect (area.removeFromTop (3.0f).reduced (1.0f, 0.0f));
        }
    }

    juce::String labels[32];
    int baseOctave = 4;      // Z = C3, Q = C4 (middle C)
    bool sustainHeld = false;
};
