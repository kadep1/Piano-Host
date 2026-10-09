# Piano Host

A small Windows app for playing piano on your computer through any VST3 instrument.

- Play from the **computer keyboard**, a **USB/MIDI keyboard**, or by clicking the on-screen 88 keys
- **Load any VST3**: click *Load VST3...*, drag `.vst3` files onto the window, or scan your VST3 folder in *Plugins...*
- Opens the plug-in's own interface (*Show UI*), and remembers each plug-in's settings
- Built-in basic piano so it makes sound before you load anything
- Sustain pedal, octave shift, velocity, master volume, level meter, panic button
- Reopens with your last instrument, audio device and settings

## Build it

You compile it once into `Piano Host.exe`. Pick one:

**A. On your PC (Visual Studio)**
1. Install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/) with the **"Desktop development with C++"** workload.
2. Open **"Developer Command Prompt for VS 2022"** from the Start menu.
3. `cd` into this folder and run `build.bat`.
4. The first build downloads JUCE and takes a few minutes. The exe ends up in
   `build\PianoHost_artefacts\Release\Piano Host.exe` (the folder opens automatically).

**B. Without installing anything (GitHub)**
Push this folder to a GitHub repo. The included workflow builds it on GitHub's Windows machines;
open the **Actions** tab, click the latest run and download **PianoHost-windows**.

The exe is self-contained (static runtime), so you can copy it anywhere.

## Playing

| Keys | Does |
|---|---|
| `Z S X D C V G B H N J M , L . ; /` | Lower octave (Z = C3) |
| `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ]` | Upper octave (Q = middle C) |
| `Space` | Sustain pedal (hold) |
| `Left` / `Right` | Octave down / up |
| `Up` / `Down` | Velocity up / down |

Blue marks under the keys show which range the computer keyboard covers; key letters appear on
the keys while the window has focus. A connected MIDI keyboard is picked up automatically, including
ones plugged in while the app is running (sustain pedal, pitch bend, mod wheel all pass through).

If the computer keys stop playing, click on the piano to give it focus. Keys don't play while a
plug-in's window is in front; click back on the main window.

## Loading instruments

- **One plug-in:** *Load VST3...* -> select the `.vst3` (click it once, then **Open**). VST3 plug-ins
  usually live in `C:\Program Files\Common Files\VST3`.
- **Many at once:** select a whole folder in *Load VST3...*, or drag the folder onto the window.
- **Scan everything:** *Plugins...* opens the plug-in manager and scans your VST3 folder the first time.
  Use **Options** there to rescan or add other folders.

Everything you've loaded stays in the instrument drop-down. Instruments are listed first, then effects.

Free instruments to try: **Spitfire LABS** (Soft Piano), **Piano One** (Sound Magic), **Keyzone Classic**,
**Pianoteq** (trial).

## Low latency

Open *Audio / MIDI...* and choose **Windows Audio (Low Latency Mode)** or **Windows Audio (Exclusive Mode)**,
then lower the buffer size (128 or 256 samples is a good target). If you hear crackles, raise it a step.

## Limits

- **VST3 only, 64-bit only.** Old VST2 (`.dll`) plug-ins aren't supported (Steinberg no longer licenses the
  VST2 SDK). Many VST2-only plug-ins also ship a VST3 version.
- **No ASIO driver option.** ASIO requires Steinberg's SDK, which can't be bundled. If you have it, add
  `JUCE_ASIO=1` and its include path in `CMakeLists.txt`. WASAPI Exclusive mode is usually close enough.
- A badly behaved plug-in can crash the app while being scanned or loaded. The plug-in manager remembers
  plug-ins that crashed during a scan and skips them next time.

## Files

```
CMakeLists.txt          build config (downloads JUCE 8.0.4)
Source/Main.cpp         app + window
Source/MainComponent.*  UI, audio/MIDI routing, VST3 loading, settings
Source/PianoKeyboard.h  on-screen piano + computer-keyboard mapping
Source/BasicPiano.h     built-in fallback piano sound
build.bat               one-click Windows build
.github/workflows/      GitHub Actions build
```

Settings are stored in `%APPDATA%\PianoHost\PianoHost.settings`.

Built with [JUCE](https://juce.com) (AGPLv3 / commercial licence).
