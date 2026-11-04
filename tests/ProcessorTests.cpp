// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// End to end tests of the AudioProcessor (needs the IR library).

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace concert7::test
{
using namespace juce;

namespace
{
    void setParam (Concert7Processor& p, const char* id, float value)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    float getParam (Concert7Processor& p, const char* id)
    {
        return p.getAPVTS().getRawParameterValue (id)->load();
    }

    /** Lets the builder finish, then runs blocks of silence until any crossfade is done. */
    void settle (Concert7Processor& p, int block = 512)
    {
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        for (int round = 0; round < 20; ++round)
        {
            p.waitUntilIdle (20000);
            MessageManager::getInstance()->runDispatchLoopUntil (20);
            for (int i = 0; i < 30; ++i) { buf.clear(); p.processBlock (buf, midi); }
            if (! p.getStatus().loading) break;
        }
    }
}

class ProcessorTest : public UnitTest
{
public:
    ProcessorTest() : UnitTest ("Processor", "concert7-processor") {}

    void runTest() override
    {
        beginTest ("prepare loads the first preset synchronously");
        if (! irFolder().isDirectory())
        {
            logMessage ("  (skipped: no IR folder)");
            return;
        }

        Concert7Processor proc;
        proc.getShared().setIRFolder (irFolder());
        proc.selectPreset ("Halls/01 Large Hall", true);
        proc.prepareToPlay (48000.0, 512);
        settle (proc);
        auto status = proc.getStatus();
        expect (status.hasIR, "IR loaded");
        expectEquals (status.presetName, String ("Large Hall"));
        expectWithinAbsoluteError (getParam (proc, ParamID::decay), status.nativeRT, 0.02f);
        logMessage ("  Large Hall native RT " + String (status.nativeRT, 2) + " s");

        beginTest ("impulse response of the processor equals the designed IR");
        {
            setParam (proc, ParamID::mix, 100.0f);
            setParam (proc, ParamID::modDepth, 0.0f);
            settle (proc);
            auto ir = proc.getCurrentIR();
            expect (ir != nullptr);
            const int len = 48000;
            AudioBuffer<float> buf (2, len);
            buf.clear();
            buf.setSample (0, 0, 1.0f); // impulse on the left input only
            MidiBuffer midi;
            for (int pos = 0; pos < len; pos += 512)
            {
                AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, std::min (512, len - pos));
                proc.processBlock (view, midi);
            }
            float maxDiff = 0.0f;
            for (int i = 0; i < len && i < ir->length(); ++i)
            {
                maxDiff = std::max (maxDiff, std::abs (buf.getSample (0, i) - ir->ch[0][(size_t) i]));
                maxDiff = std::max (maxDiff, std::abs (buf.getSample (1, i) - ir->ch[1][(size_t) i]));
            }
            expectLessThan (maxDiff, 1e-4f);
        }

        beginTest ("changing a preset resets decay unless held");
        {
            proc.selectPreset ("Rooms/01 Studio A", true);
            settle (proc);
            const auto st = proc.getStatus();
            expectEquals (st.presetName, String ("Studio A"));
            expectWithinAbsoluteError (getParam (proc, ParamID::decay), jlimit (0.2f, 30.0f, st.nativeRT), 0.02f);

            proc.getShared().setHoldDecay (true);
            setParam (proc, ParamID::decay, 5.0f);
            settle (proc);
            proc.selectPreset ("Plates/01 Bright Plate", true);
            settle (proc);
            expectWithinAbsoluteError (getParam (proc, ParamID::decay), 5.0f, 0.01f);
            auto ir = proc.getCurrentIR();
            expect (ir != nullptr && std::abs (ir->targetRT - 5.0f) < 0.01f, "IR uses the held decay");
            proc.getShared().setHoldDecay (false);
        }

        beginTest ("state save / restore");
        {
            setParam (proc, ParamID::predelay, 42.0f);
            setParam (proc, ParamID::lateRolloff, 5000.0f);
            setParam (proc, ParamID::eqMidGain, -4.0f);
            MemoryBlock state;
            proc.getStateInformation (state);

            Concert7Processor other;
            other.setStateInformation (state.getData(), (int) state.getSize());
            other.prepareToPlay (44100.0, 256);
            settle (other, 256);
            expectEquals (other.getPresetId(), proc.getPresetId());
            expectWithinAbsoluteError (getParam (other, ParamID::predelay), 42.0f, 0.01f);
            expectWithinAbsoluteError (getParam (other, ParamID::lateRolloff), 5000.0f, 1.0f);
            expectWithinAbsoluteError (getParam (other, ParamID::eqMidGain), -4.0f, 0.01f);
            expectWithinAbsoluteError (getParam (other, ParamID::decay), 5.0f, 0.01f);
            expect (other.getStatus().hasIR);
            expectEquals (other.getCurrentIR()->sampleRate, 44100.0, "IR at the new host rate");
        }

        beginTest ("random block sizes, all parameters at extremes: finite output");
        {
            Random rng (5);
            setParam (proc, ParamID::decay, 30.0f);
            setParam (proc, ParamID::lowMult, 4.0f);
            setParam (proc, ParamID::highMult, 0.2f);
            setParam (proc, ParamID::delayLevel, 0.0f);
            setParam (proc, ParamID::modDepth, 100.0f);
            setParam (proc, ParamID::duckAmount, 20.0f);
            setParam (proc, ParamID::eqHighGain, 18.0f);
            settle (proc);
            MidiBuffer midi;
            float peak = 0.0f;
            bool finite = true;
            for (int b = 0; b < 300; ++b)
            {
                const int n = 1 + rng.nextInt (2048);
                AudioBuffer<float> buf (2, n);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i) buf.setSample (c, i, (rng.nextFloat() - 0.5f) * 0.5f);
                proc.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = buf.getSample (c, i);
                        finite = finite && std::isfinite (v);
                        peak = std::max (peak, std::abs (v));
                    }
            }
            expect (finite, "no NaN / Inf");
            logMessage ("  peak " + String (peak, 2) + ", IR length " + String (proc.getCurrentIR()->seconds, 1) + " s");
            expectLessThan (peak, 20.0f);
        }

        beginTest ("tempo sync");
        {
            FixedPlayHead head;
            head.bpm = 100.0;
            proc.setPlayHead (&head);
            setParam (proc, ParamID::predelaySync, 1.0f);
            setParam (proc, ParamID::predelayNote, 10.0f); // 1/4
            AudioBuffer<float> buf (2, 64);
            MidiBuffer midi;
            proc.processBlock (buf, midi);
            expectWithinAbsoluteError (proc.getEffectivePredelayMs(), 600.0f, 0.01f);
            proc.setPlayHead (nullptr);
            setParam (proc, ParamID::predelaySync, 0.0f);
        }

        beginTest ("offline rendering never outputs a dry gap after a change");
        {
            for (auto* p : proc.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (p)) rp->setValueNotifyingHost (rp->getDefaultValue());
            setParam (proc, ParamID::mix, 100.0f);
            settle (proc);
            proc.setNonRealtime (true);
            setParam (proc, ParamID::decay, 1.0f);
            AudioBuffer<float> buf (2, 4096);
            MidiBuffer midi;
            buf.clear();
            buf.setSample (0, 0, 1.0f);
            proc.processBlock (buf, midi);
            if (auto ir = proc.getCurrentIR())
                logMessage ("  IR target after one offline block: " + String (ir->targetRT, 3) + " s (decay param "
                            + String (getParam (proc, ParamID::decay), 3) + ")");
            expect (proc.getCurrentIR() != nullptr && std::abs (proc.getCurrentIR()->targetRT - 1.0f) < 0.01f,
                    "new IR used immediately");
            expectGreaterThan (buf.getMagnitude (0, 0, 4096), 0.001f);
            proc.setNonRealtime (false);
        }

        beginTest ("mix 0 % passes the input unchanged");
        {
            setParam (proc, ParamID::mix, 0.0f);
            setParam (proc, ParamID::gain, 0.0f);
            settle (proc);
            AudioBuffer<float> buf (2, 512), ref;
            Random rng (8);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, rng.nextFloat() - 0.5f);
            ref.makeCopyOf (buf);
            MidiBuffer midi;
            proc.processBlock (buf, midi);
            float maxDiff = 0.0f;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) maxDiff = std::max (maxDiff, std::abs (buf.getSample (c, i) - ref.getSample (c, i)));
            expectLessThan (maxDiff, 1e-6f);
        }

        beginTest ("mono input layout");
        {
            Concert7Processor mono;
            AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (AudioChannelSet::mono());
            layout.inputBuses.add (AudioChannelSet::disabled());
            layout.outputBuses.add (AudioChannelSet::stereo());
            expect (mono.setBusesLayout (layout));
            mono.prepareToPlay (48000.0, 128);
            settle (mono, 128);
            AudioBuffer<float> buf (2, 128);
            buf.clear();
            buf.setSample (0, 0, 1.0f);
            MidiBuffer midi;
            setParam (mono, ParamID::mix, 100.0f);
            mono.processBlock (buf, midi);
            expect (std::isfinite (buf.getMagnitude (0, 128)));
        }

        beginTest ("editor opens and paints");
        {
            std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
            expect (ed != nullptr);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            expectEquals (img.getWidth(), ed->getWidth());
        }
    }
};

static ProcessorTest processorTest;
} // namespace concert7::test
