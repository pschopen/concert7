// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// End to end tests of the AudioProcessor (needs the IR library).

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/PostProcessing.h"

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

            proc.getShared().setKeptGroups (groupBit (ParamGroup::Decay));
            setParam (proc, ParamID::decay, 5.0f);
            settle (proc);
            proc.selectPreset ("Plates/01 Bright Plate", true);
            settle (proc);
            expectWithinAbsoluteError (getParam (proc, ParamID::decay), 5.0f, 0.01f);
            auto ir = proc.getCurrentIR();
            expect (ir != nullptr && std::abs (ir->targetRT - 5.0f) < 0.01f, "IR uses the held decay");
            proc.getShared().setKeptGroups (0);
        }

        beginTest ("kept mix keeps the mix in effect when a new preset switches the distance mode off");
        {
            setParam (proc, ParamID::mix, 40.0f);
            setParam (proc, ParamID::distOn, 1.0f);
            settle (proc);
            setParam (proc, ParamID::distance, getParam (proc, ParamID::distance) * 2.5f);
            settle (proc);
            const float inEffect = proc.getDistanceState (false).direct.mix * 100.0f;
            expectGreaterThan (std::abs (inEffect - 40.0f), 1.0f, "the distance moves the mix");
            proc.getShared().setKeptGroups (groupBit (ParamGroup::Output));
            proc.selectPreset ("Rooms/01 Studio A", true);
            settle (proc);
            expect (! proc.isDistanceOn());
            expectWithinAbsoluteError (getParam (proc, ParamID::mix), inEffect, 0.05f);

            // a kept space keeps the distance mode and the mix it moves
            setParam (proc, ParamID::distOn, 1.0f);
            settle (proc);
            const float mix = getParam (proc, ParamID::mix), distance = getParam (proc, ParamID::distance);
            proc.getShared().setKeptGroups (groupBit (ParamGroup::Space));
            proc.selectPreset ("Plates/01 Bright Plate", true);
            settle (proc);
            expect (proc.isDistanceOn());
            expectWithinAbsoluteError (getParam (proc, ParamID::mix), mix, 0.01f);
            expectWithinAbsoluteError (getParam (proc, ParamID::distance), distance, 0.01f);
            proc.getShared().setKeptGroups (0);
        }

        beginTest ("choosing a preset, also the current one again, resets all parameters");
        {
            setParam (proc, ParamID::predelay, 42.0f);
            setParam (proc, ParamID::eqMidGain, -4.0f);
            setParam (proc, ParamID::hallOn, 1.0f);
            setParam (proc, ParamID::distOn, 1.0f);
            settle (proc);
            proc.selectPreset ("Rooms/01 Studio A", true);
            settle (proc);
            for (auto* prm : proc.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                    if (rp->paramID != ParamID::decay)
                        expectWithinAbsoluteError (rp->getValue(), rp->getDefaultValue(), 1.0e-6f, rp->paramID);
            const auto st = proc.getStatus();
            expectWithinAbsoluteError (getParam (proc, ParamID::decay), jlimit (0.2f, 30.0f, st.nativeRT), 0.02f);

            setParam (proc, ParamID::mix, 80.0f);
            setParam (proc, ParamID::decay, 7.0f);
            settle (proc);
            proc.selectPreset (proc.getPresetId(), true);
            settle (proc);
            expectWithinAbsoluteError (getParam (proc, ParamID::mix), proc.getAPVTS().getParameter (ParamID::mix)->convertFrom0to1 (
                proc.getAPVTS().getParameter (ParamID::mix)->getDefaultValue()), 1.0e-4f);
            expectWithinAbsoluteError (getParam (proc, ParamID::decay), jlimit (0.2f, 30.0f, st.nativeRT), 0.02f);
        }

        beginTest ("state save / restore");
        {
            setParam (proc, ParamID::predelay, 42.0f);
            setParam (proc, ParamID::lateRolloff, 5000.0f);
            setParam (proc, ParamID::eqMidGain, -4.0f);
            setParam (proc, ParamID::decay, 5.0f);
            settle (proc);
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

        beginTest ("modulation: the late reverb is split off only when a mode needs it");
        {
            auto layout = [&] { settle (proc); auto ir = proc.getCurrentIR(); return ir == nullptr ? -1 : (int) ir->hasLate() + (int) ir->hasVariant(); };
            setParam (proc, ParamID::modMode, (float) ModMode::Drift);
            setParam (proc, ParamID::modDepth, 0.0f);
            expectEquals (layout(), 0, "depth 0");
            setParam (proc, ParamID::modDepth, 40.0f);
            expectEquals (layout(), 2, "drift");
            setParam (proc, ParamID::modMode, (float) ModMode::Chorus);
            expectEquals (layout(), 1, "chorus");
            setParam (proc, ParamID::modMode, (float) ModMode::DriftChorus);
            expectEquals (layout(), 2, "drift + chorus");
            setParam (proc, ParamID::modMode, (float) ModMode::Vibrato);
            expectEquals (layout(), 0, "vibrato");
        }

        beginTest ("modulation: drift keeps the reverb level and leaves the early reflections alone");
        {
            setParam (proc, ParamID::mix, 100.0f);
            auto impulseResponse = [&] {
                settle (proc);
                MidiBuffer midi;
                // let the tail of everything before die away
                AudioBuffer<float> silence (2, 512);
                for (int pos = 0; pos < proc.getCurrentIR()->length() + 48000; pos += 512)
                {
                    silence.clear();
                    proc.processBlock (silence, midi);
                }
                const int len = 96000;
                AudioBuffer<float> buf (2, len);
                buf.clear();
                buf.setSample (0, 0, 1.0f);
                for (int pos = 0; pos < len; pos += 512)
                {
                    AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, std::min (512, len - pos));
                    proc.processBlock (view, midi);
                }
                return buf;
            };
            setParam (proc, ParamID::modDepth, 0.0f);
            const auto plain = impulseResponse();
            setParam (proc, ParamID::modMode, (float) ModMode::Drift);
            setParam (proc, ParamID::modDepth, 100.0f);
            const auto moving = impulseResponse();
            auto ir = proc.getCurrentIR();
            expect (ir != nullptr && ir->hasVariant());

            double ePlain = 0.0, eMoving = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < plain.getNumSamples(); ++i)
                {
                    ePlain += (double) plain.getSample (c, i) * plain.getSample (c, i);
                    eMoving += (double) moving.getSample (c, i) * moving.getSample (c, i);
                }
            const double levelDb = 10.0 * std::log10 (eMoving / ePlain);
            logMessage ("  level with drift " + String (levelDb, 2) + " dB");
            expectWithinAbsoluteError (levelDb, 0.0, 1.0);

            // the early reflections until the crossover to the late reverb come out unchanged
            const int earlyEnd = (int) ((ir->onsetMs * 0.001f + ir->splitSeconds * 0.5f) * 48000.0f);
            float maxDiff = 0.0f;
            for (int i = 0; i < earlyEnd; ++i)
                maxDiff = std::max (maxDiff, std::abs (moving.getSample (0, i) - plain.getSample (0, i)));
            expectLessThan (maxDiff, 1e-4f);
            setParam (proc, ParamID::modDepth, 0.0f);
        }

        beginTest ("modulation: projects from before the modes keep their vibrato");
        {
            setParam (proc, ParamID::modMode, (float) ModMode::Drift);
            setParam (proc, ParamID::modDepth, 30.0f);
            MemoryBlock state;
            proc.getStateInformation (state);
            auto xml = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            expect (xml != nullptr);
            for (auto* e : xml->getChildIterator())
                if (e->getStringAttribute ("id") == ParamID::modMode)
                {
                    xml->removeChildElement (e, true);
                    break;
                }
            MemoryBlock old;
            AudioProcessor::copyXmlToBinary (*xml, old);

            Concert7Processor before, now;
            before.setStateInformation (old.getData(), (int) old.getSize());
            now.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (roundToInt (getParam (before, ParamID::modMode)), (int) ModMode::Vibrato);
            expectWithinAbsoluteError (getParam (before, ParamID::modDepth), 30.0f, 0.01f);
            expectEquals (roundToInt (getParam (now, ParamID::modMode)), (int) ModMode::Drift);

            // a new instance starts with the most natural mode at 0.3 Hz, the offset reaches 5 s either way
            Concert7Processor fresh;
            expectEquals (roundToInt (getParam (fresh, ParamID::modMode)), (int) ModMode::Drift);
            expectEquals (fresh.getAPVTS().getParameter (ParamID::modMode)->getCurrentValueAsText(), String ("Drift (most natural)"));
            expectWithinAbsoluteError (getParam (fresh, ParamID::modRate), 0.3f, 1e-4f);
            const auto offsetRange = fresh.getAPVTS().getParameter (ParamID::irOffset)->getNormalisableRange();
            expectEquals (offsetRange.start, -5000.0f);
            expectEquals (offsetRange.end, 5000.0f);
            setParam (proc, ParamID::modDepth, 0.0f);
        }

        beginTest ("random block sizes, all parameters at extremes: finite output");
        {
            Random rng (5);
            setParam (proc, ParamID::decay, 30.0f);
            setParam (proc, ParamID::lowMult, 4.0f);
            setParam (proc, ParamID::highMult, 0.2f);
            setParam (proc, ParamID::delayLevel, 0.0f);
            setParam (proc, ParamID::modDepth, 100.0f);
            setParam (proc, ParamID::smooth, 100.0f);
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

        beginTest ("distance mode: switching on keeps the sound, moving closer follows the model");
        {
            for (auto* prm : proc.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                    if (rp->paramID != ParamID::decay)
                        rp->setValueNotifyingHost (rp->getDefaultValue());
            proc.selectPreset ("Halls/01 Large Hall", true);
            settle (proc);
            const double sr = 48000.0;
            const int len = 4 * (int) sr;
            auto renderImpulse = [&] {
                AudioBuffer<float> buf (2, len);
                MidiBuffer midi;
                // let earlier tails die away
                for (int pos = 0; pos < 10 * (int) sr; pos += 512)
                {
                    AudioBuffer<float> silence (2, 512);
                    silence.clear();
                    proc.processBlock (silence, midi);
                }
                buf.clear();
                buf.setSample (0, 0, 1.0f);   // centred source
                buf.setSample (1, 0, 1.0f);
                for (int pos = 0; pos < len; pos += 512)
                {
                    AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, std::min (512, len - pos));
                    proc.processBlock (view, midi);
                }
                return buf;
            };
            auto midBandDrr = [&] (const AudioBuffer<float>& buf, int split) {
                double direct = 0.0, reflected = 0.0;
                for (int c = 0; c < 2; ++c)
                {
                    std::array<Biquad, 4> f;
                    f[0].highPass (sr, 500.0, 0.7071); f[1].highPass (sr, 500.0, 0.7071);
                    f[2].lowPass (sr, 2000.0, 0.7071); f[3].lowPass (sr, 2000.0, 0.7071);
                    for (int i = 0; i < len; ++i)
                    {
                        float v = buf.getSample (c, i);
                        for (auto& b : f) v = b.process (v, 0);
                        (i < split ? direct : reflected) += (double) v * v;
                    }
                }
                return 10.0 * std::log10 (direct / reflected);
            };

            const auto before = renderImpulse();
            const float shownWhileOff = proc.getDistanceModel().distance;
            setParam (proc, ParamID::distOn, 1.0f);
            settle (proc);
            const auto atStart = proc.getDistanceState();
            expectWithinAbsoluteError (getParam (proc, ParamID::distance), atStart.model.referenceDistance, 0.01f,
                                       "the distance knob starts at the reference");
            expectWithinAbsoluteError (atStart.direct.mix, 0.3f, 1.0e-4f, "greyed MIX shows the mix in effect");
            expectWithinAbsoluteError (atStart.model.referenceDistance, shownWhileOff, 1.0e-4f,
                                       "switching on starts where the greyed display showed the mix");
            expectWithinAbsoluteError (atStart.direct.gapMs, 0.0f, 0.01f);
            const auto after = renderImpulse();
            double signal = 0.0, diff = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                {
                    const double a = before.getSample (c, i), b = after.getSample (c, i);
                    signal += a * a;
                    diff += (a - b) * (a - b);
                }
            const double diffDb = 10.0 * std::log10 (diff / signal + 1e-30);
            logMessage ("  difference after switching on: " + String (diffDb, 1) + " dB");
            expectLessThan (diffDb, -100.0, "switching the mode on does not change the sound");
            const double drrStart = midBandDrr (after, roundToInt (atStart.firstReflectionMs * 0.001 * sr));
            logMessage ("  reference " + String (atStart.model.referenceDistance, 2) + " m, ratio " + String (drrStart, 1)
                        + " dB (shown " + String (atStart.drrDb, 1) + " dB)");
            expectWithinAbsoluteError (drrStart, (double) atStart.drrDb, 1.0);

            setParam (proc, ParamID::distance, atStart.model.referenceDistance * 0.5f);
            settle (proc);
            const auto closer = proc.getDistanceState();
            expectGreaterThan (closer.direct.gapMs, atStart.direct.gapMs, "closer: the first reflection comes later");
            expectGreaterThan (closer.direct.mix, 0.0f);
            expectLessThan (closer.direct.mix, atStart.direct.mix, "closer: less reverb");
            expectGreaterThan (closer.direct.highsDb, 0.0f, "closer: more presence");
            const auto near = renderImpulse();
            const int gap = roundToInt (closer.direct.gapMs * 0.001 * sr);
            expectLessThan (near.getMagnitude (0, 96, gap - 144), 1e-4f, "silence between direct sound and the reverb");
            const double drrNear = midBandDrr (near, roundToInt (closer.firstReflectionMs * 0.001 * sr));
            logMessage ("  half the distance: ratio " + String (drrNear, 1) + " dB, model change "
                        + String (closer.result.drrChangeDb, 1) + " dB");
            expectWithinAbsoluteError (drrNear - drrStart, (double) closer.result.drrChangeDb, 1.0);

            // the reference survives save / restore
            MemoryBlock saved;
            proc.getStateInformation (saved);
            Concert7Processor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError (restored.getDistanceModel().referenceDistance, atStart.model.referenceDistance, 1.0e-4f);
            expectWithinAbsoluteError (getParam (restored, ParamID::distance), atStart.model.referenceDistance * 0.5f, 0.01f,
                                       "restoring does not reset the distance");

            // switching off hands the values in effect to MIX and DIRECT: the sound stays
            setParam (proc, ParamID::distOn, 0.0f);
            settle (proc);
            expectWithinAbsoluteError (getParam (proc, ParamID::mix), closer.direct.mix * 100.0f, 0.01f);
            expectWithinAbsoluteError (getParam (proc, ParamID::directHighs), closer.direct.highsDb, 0.01f);
            expectWithinAbsoluteError (getParam (proc, ParamID::directGap), closer.direct.gapMs, 0.01f);
            const auto handedBack = renderImpulse();
            double nearEnergy = 0.0, diffEnergy = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                {
                    const double a = near.getSample (c, i), b = handedBack.getSample (c, i);
                    nearEnergy += a * a;
                    diffEnergy += (a - b) * (a - b);
                }
            const double handbackDb = 10.0 * std::log10 (diffEnergy / nearEnergy + 1e-30);
            logMessage ("  difference after switching off: " + String (handbackDb, 1) + " dB");
            expectLessThan (handbackDb, -80.0, "switching the mode off does not change the sound");
        }

        beginTest ("hall model: volume and absorption drive the decay, switching keeps it");
        {
            for (auto* prm : proc.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                    if (rp->paramID != ParamID::decay)
                        rp->setValueNotifyingHost (rp->getDefaultValue());
            proc.selectPreset ("Halls/01 Large Hall", true);
            settle (proc);
            const float native = getParam (proc, ParamID::decay);

            setParam (proc, ParamID::hallOn, 1.0f);
            settle (proc);
            const float volume = getParam (proc, ParamID::hallVolume);
            expectWithinAbsoluteError (volume, estimateHallVolume (native), volume * 1.0e-3f, "starts at the estimate");
            expectWithinAbsoluteError (proc.getEffectiveDecay(), native, native * 2.0e-3f, "switching on keeps the decay");
            expectWithinAbsoluteError (proc.getCurrentIR()->targetRT, native, native * 2.0e-3f);
            logMessage ("  Large Hall: " + formatVolume (volume) + ", absorption " + formatAbsorption (getParam (proc, ParamID::hallAbsorption)));

            setParam (proc, ParamID::hallAbsorption, 0.4f);
            settle (proc);
            const float drier = eyringRt60 (volume, 0.4f);
            expectLessThan (drier, native, "more absorption: shorter");
            expectWithinAbsoluteError (proc.getCurrentIR()->targetRT, drier, drier * 2.0e-3f);
            setParam (proc, ParamID::hallVolume, volume * 2.0f);
            settle (proc);
            const float bigger = eyringRt60 (volume * 2.0f, 0.4f);
            expectGreaterThan (bigger, drier, "larger: longer");
            expectWithinAbsoluteError (proc.getCurrentIR()->targetRT, bigger, bigger * 2.0e-3f);

            proc.getShared().setKeptGroups (groupBit (ParamGroup::Decay));
            proc.selectPreset ("Rooms/01 Studio A", true);
            settle (proc);
            expect (! proc.isHallOn(), "a new preset switches the hall model off");
            expectWithinAbsoluteError (proc.getCurrentIR()->targetRT, bigger, bigger * 2.0e-3f, "a held decay keeps the hall's decay");
            proc.getShared().setKeptGroups (0);

            setParam (proc, ParamID::hallOn, 1.0f);
            settle (proc);
            setParam (proc, ParamID::hallVolume, getParam (proc, ParamID::hallVolume) * 2.0f);
            settle (proc);
            const float hallDecay = proc.getEffectiveDecay();
            setParam (proc, ParamID::hallOn, 0.0f);
            settle (proc);
            expectWithinAbsoluteError (getParam (proc, ParamID::decay), hallDecay, hallDecay * 2.0e-3f, "switching off keeps the decay");
            expectWithinAbsoluteError (proc.getCurrentIR()->targetRT, hallDecay, hallDecay * 2.0e-3f);
        }

        beginTest ("editor opens and paints");
        {
            std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
            expect (ed != nullptr);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            expectEquals (img.getWidth(), ed->getWidth());
        }

        beginTest ("every control has a tooltip");
        {
            std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
            int controls = 0;
            std::function<void (Component&)> visit = [&] (Component& c) {
                auto* knob = dynamic_cast<Knob*> (&c);
                if (knob != nullptr || dynamic_cast<Button*> (&c) != nullptr || dynamic_cast<ComboBox*> (&c) != nullptr)
                {
                    ++controls;
                    auto* tip = dynamic_cast<TooltipClient*> (&c);
                    const auto name = knob != nullptr ? knob->getParamID() : c.getName().isNotEmpty() ? c.getName()
                                    : dynamic_cast<Button*> (&c) != nullptr ? static_cast<Button&> (c).getButtonText()
                                    : String ("combo box");
                    expect (tip != nullptr && tip->getTooltip().isNotEmpty(), "no tooltip: " + name);
                }
                if (knob != nullptr) return; // its slider carries the same tooltip
                for (auto* child : c.getChildren())
                    visit (*child);
            };
            visit (*ed);
            expectGreaterThan (controls, 60);
        }
    }
};

static ProcessorTest processorTest;
} // namespace concert7::test
