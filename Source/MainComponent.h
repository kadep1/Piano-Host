#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PianoKeyboard.h"

// Plays the audio device through an AudioProcessorPlayer, applies master volume,
// and measures output level for the meter.
class MasterOutput : public juce::AudioIODeviceCallback
{
public:
    explicit MasterOutput (juce::AudioProcessorPlayer& p) : player (p) {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& ctx) override
    {
        player.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, numSamples, ctx);

        const float target = gain.load();
        float peakNow = 0.0f;

        for (int ch = 0; ch < numOut; ++ch)
        {
            if (out[ch] == nullptr) continue;

            float g = current;
            const float step = (target - current) / (float) juce::jmax (1, numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                g += step;
                out[ch][i] *= g;
                peakNow = juce::jmax (peakNow, std::abs (out[ch][i]));
            }
        }

        current = target;
        peak.store (juce::jmax (peakNow, peak.load() * 0.9f));
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { player.audioDeviceAboutToStart (d); }
    void audioDeviceStopped() override                              { player.audioDeviceStopped(); }

    std::atomic<float> gain { 0.8f };
    std::atomic<float> peak { 0.0f };

private:
    juce::AudioProcessorPlayer& player;
    float current = 0.8f;
};

class PluginWindow;

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::MidiInputCallback,
                      private juce::MidiKeyboardState::Listener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragHover = false; repaint(); }

private:
    // MIDI routing
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void sendToInstrument (juce::MidiMessage m);
    void setSustain (bool down);
    void panic();

    // Plugins
    void loadBuiltIn();
    void loadPlugin (const juce::PluginDescription&);
    void installInstrument (std::unique_ptr<juce::AudioProcessor> p, const juce::String& id);
    void storeCurrentPluginState();
    void addPluginFiles (const juce::StringArray& paths, bool loadFirst);
    void chooseVst3File();
    void showPluginManager();
    void showAudioSettings();
    void togglePluginWindow();
    void closePluginWindow();
    void refreshInstrumentList();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void timerCallback() override;
    void setStatus (const juce::String& s, bool isError = false);
    void updateOctaveLabel();
    void focusKeyboard();

    juce::PropertiesFile& settings();

    // --- engine ---
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::AudioProcessorPlayer player;
    MasterOutput output { player };

    std::unique_ptr<juce::AudioProcessor> instrument;
    juce::String instrumentId;           // empty = built-in
    int loadGeneration = 0;

    juce::MidiKeyboardState keyboardState;
    std::atomic<bool> sustainOn { false };

    // --- UI ---
    juce::Label titleLabel, statusLabel, helpLabel, octaveLabel, cpuLabel, pedalLabel;
    juce::ComboBox instrumentBox;
    juce::TextButton loadButton { "Load VST3..." }, managerButton { "Plugins..." },
                     editorButton { "Show UI" }, audioButton { "Audio / MIDI..." }, panicButton { "Panic" };
    juce::Slider volumeSlider, velocitySlider;
    juce::Label volumeLabel, velocityLabel;
    PianoKeyboard keyboard { keyboardState };

    juce::Array<juce::PluginDescription> comboTypes;   // index i -> combo id i + 2
    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHover = false;
    bool statusIsError = false;
    float meterLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
