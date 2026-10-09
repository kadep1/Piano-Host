#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// A small built-in additive "piano" so the app makes sound before any VST3 is loaded.
// Slightly inharmonic partials, pitch-dependent decay, velocity-dependent brightness,
// and a damper release. Sustain pedal (CC64) is handled by juce::Synthesiser.
class BasicPiano : public juce::AudioProcessor
{
public:
    BasicPiano()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < 48; ++i)
            synth.addVoice (new Voice());

        synth.addSound (new Sound());
    }

    const juce::String getName() const override { return "Basic Piano (built-in)"; }

    void prepareToPlay (double sampleRate, int) override { synth.setCurrentPlaybackSampleRate (sampleRate); }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

        // gentle soft-clip so big chords never distort harshly
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                d[i] = std::tanh (d[i]);
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 2.0; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

private:
    struct Sound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct Voice : juce::SynthesiserVoice
    {
        static constexpr int numPartials = 10;

        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<Sound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            const double sr   = getSampleRate();
            const double f0   = juce::MidiMessage::getMidiNoteInHertz (note);
            const double B    = 0.00035;                                  // string stiffness
            const double pos  = juce::jlimit (0.0, 1.0, (note - 21) / 87.0);
            const double tau0 = 9.0 * std::pow (0.5, (note - 21) / 22.0) + 0.35; // seconds
            float ampSum = 0.0f;

            level = 0.2f * std::pow (velocity, 1.4f);
            const float brightness = 1.7f - 0.9f * velocity;              // harder = brighter

            for (int k = 0; k < numPartials; ++k)
            {
                const double n  = k + 1;
                const double fk = f0 * n * std::sqrt (1.0 + B * n * n);
                const bool ok   = fk < sr * 0.45;

                phase[k] = 0.0;
                inc[k]   = juce::MathConstants<double>::twoPi * fk / sr;
                amp[k]   = ok ? (float) (1.0 / std::pow (n, (double) brightness)) : 0.0f;
                env[k]   = 1.0f;
                decay[k] = (float) std::exp (-1.0 / ((tau0 / (1.0 + 0.55 * k)) * sr));
                ampSum  += amp[k];
            }

            for (auto& a : amp)   // keep loudness independent of brightness
                a /= juce::jmax (1.0f, ampSum * 0.6f);

            gainL = (float) std::cos (pos * juce::MathConstants<double>::halfPi * 0.6 + 0.47);
            gainR = (float) std::sin (pos * juce::MathConstants<double>::halfPi * 0.6 + 0.47);

            attack      = 0.0f;
            attackInc   = (float) (1.0 / (0.003 * sr));
            release     = 1.0f;
            releaseMul  = (float) std::exp (-1.0 / (0.09 * sr));
            releasing   = false;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) releasing = true;
            else              clearCurrentNote();
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive())
                return;

            auto* left  = out.getWritePointer (0);
            auto* right = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

            for (int i = start; i < start + num; ++i)
            {
                float s = 0.0f;

                for (int k = 0; k < numPartials; ++k)
                {
                    s += amp[k] * env[k] * (float) std::sin (phase[k]);
                    phase[k] += inc[k];
                    if (phase[k] > juce::MathConstants<double>::twoPi)
                        phase[k] -= juce::MathConstants<double>::twoPi;
                    env[k] *= decay[k];
                }

                attack = juce::jmin (1.0f, attack + attackInc);
                if (releasing) release *= releaseMul;

                const float v = s * level * attack * release;

                if (right != nullptr) { left[i] += v * gainL; right[i] += v * gainR; }
                else                  { left[i] += v; }

                if (release < 0.0005f || env[0] < 0.0005f)
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

        double phase[numPartials] {}, inc[numPartials] {};
        float amp[numPartials] {}, env[numPartials] {}, decay[numPartials] {};
        float level = 0, gainL = 0.7f, gainR = 0.7f;
        float attack = 0, attackInc = 0, release = 1, releaseMul = 1;
        bool releasing = false;
    };

    juce::Synthesiser synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasicPiano)
};
