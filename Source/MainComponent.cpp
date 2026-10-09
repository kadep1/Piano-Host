#include "MainComponent.h"
#include "BasicPiano.h"

using namespace juce;

namespace
{
    const Colour bgTop    (0xff1b1e25);
    const Colour bgBottom (0xff111318);
    const Colour accent   (0xff3fa7ff);
    const Colour textDim  (0xff9aa3b2);

    String stateKey (const String& pluginId)
    {
        return "state_" + String::toHexString (pluginId.hashCode64());
    }

    File defaultVst3Folder()
    {
       #if JUCE_WINDOWS
        File f ("C:\\Program Files\\Common Files\\VST3");
       #elif JUCE_MAC
        File f ("/Library/Audio/Plug-Ins/VST3");
       #else
        File f (File::getSpecialLocation (File::userHomeDirectory).getChildFile (".vst3"));
       #endif
        return f.isDirectory() ? f : File::getSpecialLocation (File::userHomeDirectory);
    }

    // Collect .vst3 bundles/files under a folder without descending into the bundles themselves.
    void findVst3 (const File& dir, Array<File>& out, int depth)
    {
        if (depth > 6) return;

        for (const auto& entry : RangedDirectoryIterator (dir, false, "*", File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();

            if (f.hasFileExtension ("vst3"))  out.add (f);
            else if (entry.isDirectory())      findVst3 (f, out, depth + 1);
        }
    }
}

//==============================================================================
class PluginWindow : public DocumentWindow
{
public:
    PluginWindow (AudioProcessor& p, std::function<void()> onCloseIn)
        : DocumentWindow (p.getName(), Colour (0xff23262e), DocumentWindow::closeButton),
          onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);

        AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)
            editor = new GenericAudioProcessorEditor (p);

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setTopLeftPosition (140, 140);
        setVisible (true);
    }

    ~PluginWindow() override { clearContentComponent(); }

    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};

//==============================================================================
MainComponent::MainComponent()
{
    PropertiesFile::Options opts;
    opts.applicationName     = "PianoHost";
    opts.filenameSuffix      = ".settings";
    opts.folderName          = "PianoHost";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (opts);

    // --- plug-in formats + saved plug-in list
    formatManager.addDefaultFormats();

    if (auto xml = settings().getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);

    // --- audio + MIDI devices
    const auto savedDevice = settings().getXmlValue ("audioDevice");
    deviceManager.initialise (0, 2, savedDevice.get(), true);

    for (const auto& d : MidiInput::getAvailableDevices())
    {
        if (savedDevice == nullptr)
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    }

    deviceManager.addMidiInputDeviceCallback ({}, this);
    deviceManager.addAudioCallback (&output);
    keyboardState.addListener (this);

    // --- UI
    titleLabel.setText ("Piano Host", dontSendNotification);
    titleLabel.setFont (FontOptions (22.0f, Font::bold));
    titleLabel.setColour (Label::textColourId, Colours::white);
    addAndMakeVisible (titleLabel);

    instrumentBox.setTextWhenNothingSelected ("Choose an instrument");
    instrumentBox.onChange = [this]
    {
        const int id = instrumentBox.getSelectedId();

        if (id == 1)
        {
            if (instrumentId.isNotEmpty() || instrument == nullptr)
                loadBuiltIn();
        }
        else if (isPositiveAndBelow (id - 2, comboTypes.size()))
        {
            const auto& d = comboTypes.getReference (id - 2);
            if (d.createIdentifierString() != instrumentId)
                loadPlugin (d);
        }

        focusKeyboard();
    };
    addAndMakeVisible (instrumentBox);

    for (auto* b : { &loadButton, &managerButton, &editorButton, &audioButton, &panicButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    instrumentBox.setMouseClickGrabsKeyboardFocus (false);
    instrumentBox.setWantsKeyboardFocus (false);

    loadButton.setColour (TextButton::buttonColourId, accent.darker (0.4f));
    loadButton.setTooltip ("Pick a .vst3 plug-in, or a folder of them. You can also drag them onto the window.");
    loadButton.onClick    = [this] { chooseVst3File(); };
    managerButton.onClick = [this] { showPluginManager(); };
    editorButton.onClick  = [this] { togglePluginWindow(); };
    audioButton.onClick   = [this] { showAudioSettings(); };
    panicButton.onClick   = [this] { panic(); focusKeyboard(); };

    auto setupSlider = [this] (Slider& s, Label& l, const String& name, double lo, double hi, double val)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setTextBoxStyle (Slider::TextBoxRight, true, 44, 20);
        s.setRange (lo, hi, 1.0);
        s.setValue (val, dontSendNotification);
        s.setMouseClickGrabsKeyboardFocus (false);
        s.setWantsKeyboardFocus (false);
        s.setColour (Slider::thumbColourId, accent);
        s.setColour (Slider::trackColourId, accent.withAlpha (0.5f));
        addAndMakeVisible (s);

        l.setText (name, dontSendNotification);
        l.setColour (Label::textColourId, textDim);
        addAndMakeVisible (l);
    };

    setupSlider (volumeSlider,   volumeLabel,   "Volume",   0, 100, settings().getIntValue ("volume", 80));
    setupSlider (velocitySlider, velocityLabel, "Velocity", 1, 127, settings().getIntValue ("velocity", 100));

    volumeSlider.onValueChange = [this]
    {
        const auto v = (float) volumeSlider.getValue() / 100.0f;
        output.gain = v * v * 1.25f;    // perceptual curve, a little headroom above unity
    };
    velocitySlider.onValueChange = [this]
    {
        keyboard.setVelocity ((float) velocitySlider.getValue() / 127.0f, false);
    };
    volumeSlider.onValueChange();
    velocitySlider.onValueChange();

    for (auto* l : { &octaveLabel, &cpuLabel, &pedalLabel, &statusLabel, &helpLabel })
    {
        l->setColour (Label::textColourId, textDim);
        addAndMakeVisible (l);
    }
    pedalLabel.setJustificationType (Justification::centred);
    cpuLabel.setJustificationType (Justification::centredRight);
    helpLabel.setJustificationType (Justification::topLeft);
    helpLabel.setFont (FontOptions (13.0f));
    helpLabel.setText ("Play:  Z S X D C V G B H N J M  (lower octave)    Q 2 W 3 E R 5 T 6 Y 7 U I  (upper octave)\n"
                       "Space = sustain pedal     Left / Right = octave     Up / Down = velocity     "
                       "A connected MIDI keyboard works too.     Drop .vst3 files here to load them.",
                       dontSendNotification);

    keyboard.onSustain  = [this] (bool down) { setSustain (down); };
    keyboard.onOctave   = [this] (int d)     { keyboard.setBaseOctave (keyboard.getBaseOctave() + d); updateOctaveLabel(); };
    keyboard.onVelocity = [this] (int d)     { velocitySlider.setValue (velocitySlider.getValue() + d * 10); };
    keyboard.setBaseOctave (settings().getIntValue ("octave", 4));
    addAndMakeVisible (keyboard);
    updateOctaveLabel();

    // --- instrument: built-in first so there's sound immediately, then the last plug-in
    const auto lastPlugin = settings().getXmlValue ("lastPlugin");
    refreshInstrumentList();
    loadBuiltIn();

    if (lastPlugin != nullptr)
    {
        PluginDescription d;
        if (d.loadFromXml (*lastPlugin))
            loadPlugin (d);
    }

    setSize (1180, 440);
    startTimerHz (25);
}

MainComponent::~MainComponent()
{
    stopTimer();
    closePluginWindow();
    chooser.reset();

    storeCurrentPluginState();
    settings().setValue ("volume",   (int) volumeSlider.getValue());
    settings().setValue ("velocity", (int) velocitySlider.getValue());
    settings().setValue ("octave",   keyboard.getBaseOctave());

    if (auto xml = deviceManager.createStateXml())
        settings().setValue ("audioDevice", xml.get());

    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (&output);
    deviceManager.closeAudioDevice();

    player.setProcessor (nullptr);
    keyboardState.removeListener (this);
    knownPlugins.removeChangeListener (this);
    instrument.reset();

    settings().saveIfNeeded();
}

PropertiesFile& MainComponent::settings() { return *appProperties.getUserSettings(); }

//==============================================================================
// MIDI

void MainComponent::handleIncomingMidiMessage (MidiInput*, const MidiMessage& m)
{
    // Notes go through the keyboard state so they light up on screen;
    // everything else (pedals, pitch bend, mod wheel...) goes straight to the instrument.
    if (m.isNoteOnOrOff() || m.isAllNotesOff())
    {
        keyboardState.processNextMidiEvent (m);
        return;
    }

    if (m.isSustainPedalOn())  sustainOn = true;
    if (m.isSustainPedalOff()) sustainOn = false;

    sendToInstrument (m);
}

void MainComponent::handleNoteOn (MidiKeyboardState*, int channel, int note, float velocity)
{
    sendToInstrument (MidiMessage::noteOn (channel, note, velocity));
}

void MainComponent::handleNoteOff (MidiKeyboardState*, int channel, int note, float velocity)
{
    sendToInstrument (MidiMessage::noteOff (channel, note, velocity));
}

void MainComponent::sendToInstrument (MidiMessage m)
{
    m.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
    player.getMidiMessageCollector().addMessageToQueue (m);
}

void MainComponent::setSustain (bool down)
{
    sustainOn = down;
    sendToInstrument (MidiMessage::controllerEvent (1, 64, down ? 127 : 0));
}

void MainComponent::panic()
{
    keyboardState.allNotesOff (0);
    sustainOn = false;

    for (int ch = 1; ch <= 16; ++ch)
    {
        sendToInstrument (MidiMessage::controllerEvent (ch, 64, 0));
        sendToInstrument (MidiMessage::allNotesOff (ch));
        sendToInstrument (MidiMessage::allSoundOff (ch));
    }

    setStatus ("All notes off.");
}

//==============================================================================
// Instruments

void MainComponent::loadBuiltIn()
{
    ++loadGeneration;   // cancel any plug-in still loading
    installInstrument (std::make_unique<BasicPiano>(), {});
    settings().removeValue ("lastPlugin");
    setStatus ("Using the built-in piano. Load a VST3 for a better sound.");
}

void MainComponent::loadPlugin (const PluginDescription& desc)
{
    setStatus ("Loading " + desc.name + "...");
    const int generation = ++loadGeneration;

    double sampleRate = 44100.0;
    int blockSize = 512;

    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = dev->getCurrentSampleRate();
        blockSize  = dev->getCurrentBufferSizeSamples();
    }

    SafePointer<MainComponent> safe (this);

    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [safe, generation, desc] (std::unique_ptr<AudioPluginInstance> inst, const String& error)
        {
            if (safe == nullptr || generation != safe->loadGeneration)
                return;

            if (inst == nullptr)
            {
                safe->setStatus ("Couldn't load " + desc.name + ": " + (error.isNotEmpty() ? error : String ("unknown error")), true);
                safe->refreshInstrumentList();
                return;
            }

            const bool takesMidi = inst->acceptsMidi();
            safe->installInstrument (std::move (inst), desc.createIdentifierString());

            if (auto xml = desc.createXml())
                safe->settings().setValue ("lastPlugin", xml.get());

            if (takesMidi)
                safe->setStatus ("Loaded " + desc.name + " (" + desc.pluginFormatName + ")"
                                 + (desc.manufacturerName.isNotEmpty() ? " by " + desc.manufacturerName : String()));
            else
                safe->setStatus (desc.name + " loaded, but it's an effect that doesn't accept MIDI, so it won't make sound from notes.", true);
        });
}

void MainComponent::installInstrument (std::unique_ptr<AudioProcessor> p, const String& id)
{
    closePluginWindow();
    storeCurrentPluginState();

    if (id.isNotEmpty())
    {
        MemoryBlock mb;
        if (mb.fromBase64Encoding (settings().getValue (stateKey (id))) && mb.getSize() > 0)
            p->setStateInformation (mb.getData(), (int) mb.getSize());
    }

    auto old = std::move (instrument);
    instrument = std::move (p);
    instrumentId = id;

    player.setProcessor (instrument.get());   // thread-safe swap; prepares the new one
    old.reset();

    // Re-assert the pedal state on the new instrument.
    if (sustainOn)
        sendToInstrument (MidiMessage::controllerEvent (1, 64, 127));

    editorButton.setEnabled (id.isNotEmpty());
    refreshInstrumentList();
    focusKeyboard();
}

void MainComponent::storeCurrentPluginState()
{
    if (instrument == nullptr || instrumentId.isEmpty())
        return;

    MemoryBlock mb;
    instrument->getStateInformation (mb);

    if (mb.getSize() > 0)
        settings().setValue (stateKey (instrumentId), mb.toBase64Encoding());
}

void MainComponent::refreshInstrumentList()
{
    auto types = knownPlugins.getTypes();

    std::sort (types.begin(), types.end(), [] (const PluginDescription& a, const PluginDescription& b)
    {
        if (a.isInstrument != b.isInstrument) return a.isInstrument;
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    comboTypes.clearQuick();
    instrumentBox.clear (dontSendNotification);
    instrumentBox.addItem ("Basic Piano (built-in)", 1);

    int selected = instrumentId.isEmpty() ? 1 : 0;
    bool headingInstruments = false, headingOther = false;

    for (const auto& t : types)
    {
        if (t.isInstrument && ! headingInstruments) { instrumentBox.addSectionHeading ("Instruments");              headingInstruments = true; }
        if (! t.isInstrument && ! headingOther)     { instrumentBox.addSectionHeading ("Other plug-ins / effects"); headingOther = true; }

        const int itemId = comboTypes.size() + 2;
        comboTypes.add (t);
        instrumentBox.addItem (t.name + (t.manufacturerName.isNotEmpty() ? "   -  " + t.manufacturerName : String()), itemId);

        if (t.createIdentifierString() == instrumentId)
            selected = itemId;
    }

    if (selected == 0 && instrument != nullptr)
        instrumentBox.setText (instrument->getName(), dontSendNotification);
    else
        instrumentBox.setSelectedId (selected, dontSendNotification);
}

void MainComponent::changeListenerCallback (ChangeBroadcaster*)
{
    refreshInstrumentList();

    if (auto xml = knownPlugins.createXml())
        settings().setValue ("knownPlugins", xml.get());

    settings().saveIfNeeded();
}

void MainComponent::addPluginFiles (const StringArray& paths, bool loadFirst)
{
    Array<File> candidates;

    for (const auto& p : paths)
    {
        const File f (p);
        if (f.isDirectory() && ! f.hasFileExtension ("vst3")) findVst3 (f, candidates, 0);
        else                                                  candidates.add (f);
    }

    OwnedArray<PluginDescription> found;
    StringArray failed;

    for (const auto& f : candidates)
    {
        bool any = false;

        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* format = formatManager.getFormat (i);

            if (format->fileMightContainThisPluginType (f.getFullPathName()))
            {
                const int before = found.size();
                knownPlugins.scanAndAddFile (f.getFullPathName(), true, found, *format);
                any = any || found.size() > before;
            }
        }

        if (! any)
            failed.add (f.getFileName());
    }

    if (found.isEmpty())
    {
        setStatus (candidates.isEmpty() ? "No .vst3 plug-ins found there."
                                        : "Couldn't open: " + failed.joinIntoString (", ")
                                          + ". Make sure it's a 64-bit VST3 plug-in.", true);
        return;
    }

    if (loadFirst && candidates.size() == 1)
    {
        const PluginDescription* pick = found.getFirst();
        for (auto* d : found)
            if (d->isInstrument) { pick = d; break; }

        loadPlugin (*pick);
    }
    else
    {
        setStatus ("Added " + String (found.size()) + " plug-in" + (found.size() == 1 ? "" : "s")
                   + " to the instrument list." + (failed.isEmpty() ? String() : "  Skipped: " + failed.joinIntoString (", ")));
    }
}

void MainComponent::chooseVst3File()
{
    const File start (settings().getValue ("lastFolder", defaultVst3Folder().getFullPathName()));

    // JUCE's own browser (not the native one) so .vst3 bundle folders can be picked directly.
    chooser = std::make_unique<FileChooser> ("Choose a VST3 plug-in, or a folder of them", start, "*.vst3", false);

    const auto browserFlags = FileBrowserComponent::openMode
                     | FileBrowserComponent::canSelectFiles
                     | FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync (browserFlags, [this] (const FileChooser& fc)
    {
        const auto result = fc.getResult();

        if (result != File())
        {
            settings().setValue ("lastFolder", result.getParentDirectory().getFullPathName());
            addPluginFiles ({ result.getFullPathName() }, true);
        }

        focusKeyboard();
    });
}

void MainComponent::showPluginManager()
{
    const auto deadMansPedal = settings().getFile().getSiblingFile ("RecentlyCrashedPluginsList");
    auto* list = new PluginListComponent (formatManager, knownPlugins, deadMansPedal, &settings(), true);
    list->setSize (760, 480);

    DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Plug-in Manager  -  use Options to scan your VST3 folder";
    o.dialogBackgroundColour = bgTop;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();

    // First time: start a scan straight away.
    if (knownPlugins.getNumTypes() == 0)
    {
        Component::SafePointer<PluginListComponent> safeList (list);

        MessageManager::callAsync ([this, safeList]
        {
            if (safeList == nullptr) return;

            for (int i = 0; i < formatManager.getNumFormats(); ++i)
                if (auto* f = formatManager.getFormat (i); f->getName() == "VST3")
                    safeList->scanFor (*f);
        });
    }
}

void MainComponent::showAudioSettings()
{
    auto* selector = new AudioDeviceSelectorComponent (deviceManager,
                                                       0, 0,      // inputs
                                                       1, 2,      // outputs
                                                       true,      // MIDI inputs
                                                       false,     // MIDI output
                                                       true,      // stereo pairs
                                                       false);
    selector->setSize (560, 480);

    DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio / MIDI settings";
    o.dialogBackgroundColour = bgTop;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::togglePluginWindow()
{
    if (pluginWindow != nullptr)
    {
        closePluginWindow();
        return;
    }

    if (instrument == nullptr || instrumentId.isEmpty())
        return;

    SafePointer<MainComponent> safe (this);
    pluginWindow = std::make_unique<PluginWindow> (*instrument, [safe]
    {
        MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePluginWindow(); });
    });

    editorButton.setButtonText ("Hide UI");
}

void MainComponent::closePluginWindow()
{
    pluginWindow.reset();
    editorButton.setButtonText ("Show UI");
    focusKeyboard();
}

//==============================================================================
// UI

void MainComponent::setStatus (const String& s, bool isError)
{
    statusIsError = isError;
    statusLabel.setText (s, dontSendNotification);
    statusLabel.setColour (Label::textColourId, isError ? Colour (0xffff7a6b) : Colours::white.withAlpha (0.85f));
}

void MainComponent::updateOctaveLabel()
{
    octaveLabel.setText ("Keys: " + MidiMessage::getMidiNoteName (keyboard.lowestMappedNote(), true, true, 4)
                         + " - " + MidiMessage::getMidiNoteName (keyboard.highestMappedNote(), true, true, 4),
                         dontSendNotification);
}

void MainComponent::focusKeyboard()
{
    if (isShowing())
        keyboard.grabKeyboardFocus();
}

void MainComponent::mouseDown (const MouseEvent&) { focusKeyboard(); }

void MainComponent::timerCallback()
{
    // CPU + pedal
    cpuLabel.setText ("CPU " + String (roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%", dontSendNotification);

    const bool ped = sustainOn.load();
    pedalLabel.setText (ped ? "SUSTAIN" : "pedal", dontSendNotification);
    pedalLabel.setColour (Label::textColourId, ped ? Colours::black : textDim);
    pedalLabel.setColour (Label::backgroundColourId, ped ? accent : Colours::transparentBlack);

    // Meter
    const float p = output.peak.load();
    output.peak.store (p * 0.6f);
    meterLevel = jmax (p, meterLevel * 0.85f);
    repaint (volumeSlider.getBounds().withY (volumeSlider.getBottom() + 2).withHeight (4));

    // Enable newly plugged-in MIDI keyboards automatically.
    static int tick = 0;
    if (++tick % 50 == 0)
    {
        static StringArray seen;
        for (const auto& d : MidiInput::getAvailableDevices())
        {
            if (! seen.contains (d.identifier))
            {
                if (tick > 50) // anything that appears after start-up
                    deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
                seen.add (d.identifier);
            }
        }
    }

    // Keep keys playable when nothing else wants focus.
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! keyboard.hasKeyboardFocus (false))
        focusKeyboard();
}

void MainComponent::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (bgTop, 0, 0, bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (60, 0.0f, (float) getWidth());

    // level meter under the volume slider
    auto m = volumeSlider.getBounds().withY (volumeSlider.getBottom() + 2).withHeight (4).toFloat();
    g.setColour (Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (m, 2.0f);
    const float lvl = jlimit (0.0f, 1.0f, meterLevel);
    g.setColour (lvl > 0.98f ? Colour (0xffff5a4a) : accent);
    g.fillRoundedRectangle (m.withWidth (m.getWidth() * std::sqrt (lvl)), 2.0f);

    if (dragHover)
    {
        auto r = getLocalBounds().reduced (8).toFloat();
        g.setColour (accent.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (r, 10.0f, 2.0f);
        g.setFont (FontOptions (22.0f, Font::bold));
        g.drawText ("Drop to load VST3", r, Justification::centred);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto top = r.removeFromTop (40);
    titleLabel.setBounds (top.removeFromLeft (130));
    panicButton.setBounds   (top.removeFromRight (70).reduced (3, 4));
    audioButton.setBounds   (top.removeFromRight (120).reduced (3, 4));
    editorButton.setBounds  (top.removeFromRight (90).reduced (3, 4));
    managerButton.setBounds (top.removeFromRight (90).reduced (3, 4));
    loadButton.setBounds    (top.removeFromRight (110).reduced (3, 4));
    instrumentBox.setBounds (top.reduced (3, 4));

    r.removeFromTop (16);
    auto row = r.removeFromTop (28);
    volumeLabel.setBounds (row.removeFromLeft (60));
    volumeSlider.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (20);
    velocityLabel.setBounds (row.removeFromLeft (62));
    velocitySlider.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (20);
    cpuLabel.setBounds (row.removeFromRight (80));
    pedalLabel.setBounds (row.removeFromRight (80).reduced (4, 3));
    octaveLabel.setBounds (row);

    r.removeFromTop (8);
    statusLabel.setBounds (r.removeFromTop (24));

    const int keyboardHeight = jlimit (110, 300, r.getHeight() - 44);
    keyboard.setBounds (r.removeFromBottom (keyboardHeight));
    r.removeFromBottom (6);
    helpLabel.setBounds (r);
}

//==============================================================================
// Drag & drop

bool MainComponent::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& f : files)
        if (File (f).hasFileExtension ("vst3") || File (f).isDirectory())
            return true;

    return false;
}

void MainComponent::filesDropped (const StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    addPluginFiles (files, true);
    focusKeyboard();
}
