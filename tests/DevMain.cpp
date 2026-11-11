// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// concert7-dev: developer tool for testing the plugin without a DAW.
//
//   concert7-dev test     [--irs DIR] [--split DIR] [--category dsp|library|processor] [--seed N]
//                         (--split: a library of split captures, also for the other commands; --own DIR for
//                         render / ir / snapshot / bench: a folder of other impulse responses)
//   concert7-dev render   --in IN.wav --out OUT.wav [--irs DIR] [--preset ID] [--set id=value ...]
//                         [--block N] [--tail SECONDS] [--sr RATE]
//   concert7-dev ir       --out IR.wav [--preset ID] [--set id=value ...] [--sr RATE]
//   concert7-dev snapshot --out UI.png [--tab 0..4] [--welcome] [--preset ID] [--scale S] [--keep decay,output]
//                         [--browser SEARCH]
//   concert7-dev bench    [--preset ID] [--seconds S] [--block N] [--sr RATE] [--lowcpu] [--undersample] [--realtime] [--set ...]
//   concert7-dev presets  [--irs DIR]            (lists presets with their analysed reverb times)
//   concert7-dev analyze  --preset ID [--sr RATE] (per band analysis of one preset)
//   concert7-dev fingerprint [--sr RATE]         (hashes of designed responses of synthetic sources over a matrix
//                                                 of settings: the same before and after a change of the designer
//                                                 means that no response has changed)
//   concert7-dev shape    [--factor F] [--mode M] [--preset ID] [--valid] [--verbose]
//                                                (how far DECAY changes the shape of the presets' decays: level against
//                                                 the recording stretched in time, per band)
//
//   --no-shape with any command: DECAY moves a decay that is not a straight line in dB like an exponential one (as up
//   to 0.2.1-beta), instead of stretching its shape in time

#include "TestHelpers.h"
#include <iostream>
#if ! JUCE_WINDOWS
 #include <sys/resource.h>
#endif

namespace concert7::test { bool setAudioUnitOfflineRender (void* audioUnit, bool offline); }
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace concert7;

namespace
{
    /** Accepts both "--name value" and "--name=value". */
    String argValue (const ArgumentList& args, const String& name, const String& fallback = {})
    {
        for (int i = 0; i < args.size(); ++i)
        {
            const auto& t = args[i].text;
            if (t.startsWith (name + "="))
                return t.fromFirstOccurrenceOf ("=", false, false).unquoted();
            if (t == name && i + 1 < args.size())
                return args[i + 1].text.unquoted();
        }
        return fallback;
    }

    bool hasArg (const ArgumentList& args, const String& name)
    {
        for (int i = 0; i < args.size(); ++i)
            if (args[i].text == name || args[i].text.startsWith (name + "="))
                return true;
        return false;
    }

    File fileArg (const ArgumentList& args, const String& name)
    {
        const auto v = argValue (args, name);
        return v.isEmpty() ? File() : File::getCurrentWorkingDirectory().getChildFile (v);
    }

    /** --set id=value: a number, or for a choice one of its names or the start of one (decayMode=Proportional,
        decayMode=Prop). False for an unknown parameter or a value that cannot be read (it would become 0). */
    bool applySettings (Concert7Processor& proc, const ArgumentList& args)
    {
        auto isNumber = [] (const String& text) {
            const char* start = text.toRawUTF8();
            char* end = nullptr;
            std::strtod (start, &end);
            return end != start && *end == 0;
        };
        bool ok = true;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i].text != "--set" || i + 1 >= args.size()) continue;
            const auto kv = args[i + 1].text;
            const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto text = kv.fromFirstOccurrenceOf ("=", false, false).trim();
            auto* p = proc.getAPVTS().getParameter (id);
            if (p == nullptr)
            {
                std::cerr << "unknown parameter " << id << std::endl;
                ok = false;
                continue;
            }
            float value = text.getFloatValue();
            if (! isNumber (text))
            {
                auto* choice = dynamic_cast<AudioParameterChoice*> (p);
                if (choice == nullptr)
                {
                    std::cerr << id << ": not a number: " << text << std::endl;
                    ok = false;
                    continue;
                }
                // the name itself, else the only one that starts with the text, else the only one that contains it
                const auto names = choice->getAllValueStrings();
                int index = -1, matches = 0;
                for (auto match : { std::function<bool (const String&)> ([&] (const String& n) { return n.equalsIgnoreCase (text); }),
                                    std::function<bool (const String&)> ([&] (const String& n) { return n.startsWithIgnoreCase (text); }),
                                    std::function<bool (const String&)> ([&] (const String& n) { return n.containsIgnoreCase (text); }) })
                {
                    matches = 0;
                    for (int k = 0; k < names.size(); ++k)
                        if (match (names[k])) { index = k; ++matches; }
                    if (matches > 0) break;
                }
                if (matches != 1) index = -1;
                if (index < 0)
                {
                    std::cerr << id << ": " << (matches > 1 ? "more than one value matches " : "no value ") << text
                              << " (" << names.joinIntoString (", ") << ")" << std::endl;
                    ok = false;
                    continue;
                }
                value = (float) index;
            }
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            MessageManager::getInstance()->runDispatchLoopUntil (10); // e.g. distance mode sets its start
            std::cout << "  " << id << " = " << p->getCurrentValueAsText() << std::endl;
        }
        return ok;
    }

    void settle (Concert7Processor& proc, int block)
    {
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        for (int round = 0; round < 50; ++round)
        {
            proc.waitUntilIdle (60000);
            MessageManager::getInstance()->runDispatchLoopUntil (20);
            for (int i = 0; i < 40; ++i) { buf.clear(); proc.processBlock (buf, midi); }
            if (! proc.getStatus().loading) break;
        }
    }

    /** Creates a processor with test settings, an IR folder and an optional preset (nullptr where a --set fails). */
    std::unique_ptr<Concert7Processor> makeProcessor (const ArgumentList& args, double sr, int block, bool userPreset = true)
    {
        auto proc = std::make_unique<Concert7Processor>();
        if (test::irFolder().isDirectory() && proc->getShared().getIRFolder() != test::irFolder())
            proc->getShared().setIRFolder (test::irFolder());
        if (test::splitFolder().isDirectory() && proc->getShared().getSplitFolder() != test::splitFolder())
            proc->getShared().setSplitFolder (test::splitFolder());
        if (hasArg (args, "--own"))
            proc->getShared().setOwnFolder (fileArg (args, "--own"));
        if (hasArg (args, "--preset"))
            proc->selectPreset (argValue (args, "--preset"), userPreset);
        proc->setLowCpu (hasArg (args, "--lowcpu"));
        proc->setUnderSampling (hasArg (args, "--undersample"));
        proc->prepareToPlay (sr, block);
        settle (*proc, block);
        if (! applySettings (*proc, args))
            return nullptr;
        settle (*proc, block);
        const auto st = proc->getStatus();
        std::cout << "Preset: " << proc->getPresetId() << "  (native RT " << st.nativeRT << " s, "
                  << (st.trueStereo ? "true stereo" : "stereo") << ", file " << st.fileRate << " Hz)" << std::endl;
        if (! st.hasIR)
            std::cerr << "WARNING: no impulse response loaded - " << st.message << std::endl;
        return proc;
    }

    int runTests (const ArgumentList& args)
    {
        UnitTestRunner runner;
        runner.setAssertOnFailure (false);
        const auto seed = argValue (args, "--seed", "0").getLargeIntValue();
        Array<UnitTest*> tests;
        const auto category = argValue (args, "--category");
        for (auto* t : UnitTest::getAllTests())
            if (t->getCategory().startsWith ("concert7")
                && (category.isEmpty() || t->getCategory() == "concert7-" + category))
                tests.add (t);
        runner.runTests (tests, seed);

        int failures = 0, passes = 0;
        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            failures += runner.getResult (i)->failures;
            passes += runner.getResult (i)->passes;
        }
        std::cout << "\n==== " << passes << " checks passed, " << failures << " failed ====" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    int render (const ArgumentList& args)
    {
        const File in = fileArg (args, "--in");
        const File out = fileArg (args, "--out");
        AudioBuffer<float> input;
        double fileRate = 48000.0;
        if (! test::readAudio (in, input, fileRate)) { std::cerr << "cannot read " << in.getFullPathName() << std::endl; return 1; }
        const double sr = argValue (args, "--sr", String (fileRate)).getDoubleValue();
        const int block = argValue (args, "--block", "512").getIntValue();
        const double tail = argValue (args, "--tail", "3").getDoubleValue();
        if (std::abs (sr - fileRate) > 1.0) { std::cerr << "--sr must match the input file rate" << std::endl; return 1; }

        auto proc = makeProcessor (args, sr, block);
        if (proc == nullptr) return 1;
        const int total = input.getNumSamples() + (int) (tail * sr);
        AudioBuffer<float> buf (2, total);
        buf.clear();
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, input, std::min (c, input.getNumChannels() - 1), 0, input.getNumSamples());

        MidiBuffer midi;
        const auto t0 = Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += block)
        {
            AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, std::min (block, total - pos));
            proc->processBlock (view, midi);
        }
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        std::cout << "Rendered " << total / sr << " s in " << ms / 1000.0 << " s ("
                  << (total / sr) / (ms / 1000.0) << "x realtime), peak " << Decibels::gainToDecibels (buf.getMagnitude (0, total)) << " dBFS" << std::endl;
        return test::writeWav (out, buf, sr) ? 0 : 1;
    }

    int exportIR (const ArgumentList& args)
    {
        const double sr = argValue (args, "--sr", "48000").getDoubleValue();
        auto proc = makeProcessor (args, sr, 512);
        if (proc == nullptr) return 1;
        auto ir = proc->getCurrentIR();
        if (ir == nullptr) return 1;
        AudioBuffer<float> buf (4, ir->length());
        for (int c = 0; c < 4; ++c)
            if (! ir->ch[(size_t) c].empty())
                buf.copyFrom (c, 0, ir->ch[(size_t) c].data(), ir->length());
            else
                buf.clear (c, 0, ir->length());
        std::cout << "IR: " << ir->seconds << " s, target RT " << ir->targetRT << " s, native " << ir->nativeRT << " s" << std::endl;
        return test::writeWav (fileArg (args, "--out"), buf, sr) ? 0 : 1;
    }

    int snapshot (const ArgumentList& args)
    {
        auto proc = makeProcessor (args, 48000.0, 512);
        if (proc == nullptr) return 1;
        // play some noise so the meters show something
        AudioBuffer<float> buf (2, 512);
        MidiBuffer midi;
        Random rng (1);
        for (int b = 0; b < 20; ++b)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, (rng.nextFloat() - 0.5f) * 0.6f);
            proc->processBlock (buf, midi);
        }
        if (! hasArg (args, "--welcome"))
            proc->getShared().setWelcomeDone (true);
        if (hasArg (args, "--keep")) // e.g. --keep decay,output: the padlocks of kept groups
        {
            GroupMask kept = 0;
            for (auto& key : StringArray::fromTokens (argValue (args, "--keep"), ",", {}))
                for (int g = 0; g < kNumParamGroups; ++g)
                    if (key == groupKey ((ParamGroup) g)) kept |= groupBit ((ParamGroup) g);
            proc->getShared().setKeptGroups (kept);
        }

        std::unique_ptr<AudioProcessorEditor> editor (proc->createEditor());
        auto* ed = dynamic_cast<Concert7Editor*> (editor.get());
        const float scale = argValue (args, "--scale", "1").getFloatValue();
        editor->setSize (roundToInt (MainPanel::kWidth * scale), roundToInt (MainPanel::kHeight * scale));
        if (hasArg (args, "--tab") && ed != nullptr)
            ed->getPanel().showTab (argValue (args, "--tab").getIntValue());
        if (hasArg (args, "--browser") && ed != nullptr) // the preset browser with a few user presets
        {
            String error;
            proc->saveUserPreset ("Vocal Plate Dark", { "vocals", "dark" }, "Warm, short", error);
            proc->saveUserPreset ("Drum Room", { "drums" }, {}, error);
            proc->getShared().getUserPresets().setFavourite ("Halls/01 Large Hall", true);
            proc->getShared().getUserPresets().setLibraryTags ("Halls/01 Large Hall", { "orchestra" });
            ed->getPanel().showBrowser (true);
            ed->getPanel().getBrowser().setSearchText (argValue (args, "--browser"));
        }
        if (ed != nullptr)
            ed->getPanel().showWelcome (hasArg (args, "--welcome"));
        if (auto* overlay = ed != nullptr ? ed->getPanel().getWelcome() : nullptr)
        {
            if (hasArg (args, "--drag")) overlay->setDragHighlight (true);
            if (hasArg (args, "--install"))
            {
                // run the real install flow (into a temporary folder)
                overlay->acceptFile (fileArg (args, "--install"));
                const bool waitForEnd = ! hasArg (args, "--busy");
                for (int i = 0; i < 600; ++i)
                {
                    MessageManager::getInstance()->runDispatchLoopUntil (50);
                    const auto phase = proc->getShared().getInstaller().getPhase();
                    const bool finished = phase == IRInstaller::Phase::Done || phase == IRInstaller::Phase::Failed;
                    if (waitForEnd ? finished : (phase == IRInstaller::Phase::Extracting && proc->getShared().getInstaller().getProgress() > 0.3f))
                        break;
                }
                overlay->refresh();
            }
        }

        MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const File out = fileArg (args, "--out");
        out.deleteFile();
        FileOutputStream os (out);
        PNGImageFormat png;
        const bool ok = os.openedOk() && png.writeImageToStream (img, os);
        std::cout << "Snapshot " << img.getWidth() << "x" << img.getHeight() << " -> " << out.getFullPathName() << std::endl;
        editor.reset();
        return ok ? 0 : 1;
    }

    int bench (const ArgumentList& args)
    {
        const double sr = argValue (args, "--sr", "48000").getDoubleValue();
        const int block = argValue (args, "--block", "128").getIntValue();
        const double seconds = argValue (args, "--seconds", "20").getDoubleValue();
        auto proc = makeProcessor (args, sr, block);
        if (proc == nullptr) return 1;
        const int total = (int) (seconds * sr);
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        Random rng (2);
        double worst = 0.0;
        const bool realtime = hasArg (args, "--realtime");
        // CPU time of the whole process: the tail convolution runs on background threads
        auto processCpuSeconds = [] {
           #if JUCE_WINDOWS
            return 0.0;
           #else
            rusage u {};
            getrusage (RUSAGE_SELF, &u);
            return (double) u.ru_utime.tv_sec + u.ru_utime.tv_usec * 1.0e-6 + (double) u.ru_stime.tv_sec + u.ru_stime.tv_usec * 1.0e-6;
           #endif
        };
        const auto cpu0 = processCpuSeconds();
        const auto t0 = Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += block)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, rng.nextFloat() - 0.5f);
            const auto b0 = Time::getMillisecondCounterHiRes();
            proc->processBlock (buf, midi);
            worst = std::max (worst, Time::getMillisecondCounterHiRes() - b0);
            if (realtime) // paced like an audio device: background threads get the time they have there
                while (Time::getMillisecondCounterHiRes() < t0 + (pos + block) * 1000.0 / sr)
                    Thread::sleep (0);
        }
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        const double blockMs = block / sr * 1000.0;
        std::cout << "IR length " << proc->getCurrentIR()->seconds << " s, block " << block << "\n"
                  << "average CPU " << 100.0 * (ms / 1000.0) / seconds << " % of one core, worst block "
                  << worst << " ms (" << 100.0 * worst / blockMs << " % of the block period)\n"
                  << "process CPU (all threads) " << 100.0 * (processCpuSeconds() - cpu0) / seconds
                  << " % of one core" << std::endl;
        return 0;
    }

    int analyzePreset (const ArgumentList& args)
    {
        std::shared_ptr<SourceIR> src;
        String name;
        if (hasArg (args, "--file"))
        {
            // analyse any (e.g. exported) 1, 2 or 4 channel WAV with the same code as the presets
            AudioBuffer<float> buf;
            double rate = 48000.0;
            if (! test::readAudio (fileArg (args, "--file"), buf, rate)) { std::cerr << "cannot read file" << std::endl; return 1; }
            std::array<std::vector<float>, 4> ch;
            for (int c = 0; c < 4; ++c)
            {
                const int srcCh = buf.getNumChannels() >= 4 ? c : (c < 2 ? 0 : std::min (1, buf.getNumChannels() - 1));
                ch[(size_t) c].assign (buf.getReadPointer (srcCh), buf.getReadPointer (srcCh) + buf.getNumSamples());
            }
            src = makeSourceIR (std::move (ch), buf.getNumChannels() >= 4, rate, rate, "file");
            name = fileArg (args, "--file").getFileName();
        }
        else
        {
            SharedResourcePointer<SharedState> shared;
            if (test::irFolder().isDirectory()) shared->setIRFolder (test::irFolder());
            if (test::splitFolder().isDirectory()) shared->setSplitFolder (test::splitFolder());
            IRPreset p;
            if (! shared->getPreset (argValue (args, "--preset"), p)) { std::cerr << "unknown preset" << std::endl; return 1; }
            String err;
            src = loadSourceIR (p, argValue (args, "--sr", "48000").getDoubleValue(), err);
            if (src == nullptr) { std::cerr << err << std::endl; return 1; }
            name = p.id;
        }
        std::cout << name << ": onset " << src->onset << " samples, native RT " << src->nativeRT << " s, length "
                  << src->length() / src->sampleRate << " s\n";
        std::cout << "  band Hz     RT s   valid end s   intercept dB   level dB @ 0.1 0.25 0.5 1 1.5 2 3 4 6 s\n";
        const int hop = src->sampleRate <= 50000.0 ? 512 : (src->sampleRate <= 100000.0 ? 1024 : 2048);
        for (size_t b = 0; b < src->bandFreq.size(); ++b)
        {
            String line = "  " + String (roundToInt (src->bandFreq[b])).paddedLeft (' ', 7) + "  "
                        + String (src->bandRT[b], 2).paddedLeft (' ', 6) + "  "
                        + String (src->bandValidEnd[b], 2).paddedLeft (' ', 10) + "  "
                        + String (src->bandIntercept[b], 1).paddedLeft (' ', 12) + "   ";
            for (double t : { 0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0 })
            {
                const int m = (int) ((src->onset + t * src->sampleRate) / hop);
                line += m < (int) src->bandLevelDb[b].size() ? String (src->bandLevelDb[b][(size_t) m], 1).paddedLeft (' ', 7) : String ("      -");
            }
            std::cout << line << "\n";
        }
        return 0;
    }

    /** Loads the built plugin binary (VST3 bundle or AU identifier) like a DAW does and exercises it. */
    int hostPlugin (const ArgumentList& args)
    {
        const auto target = argValue (args, "--plugin");
        AudioPluginFormatManager fm;
        addDefaultFormatsToManager (fm);

        OwnedArray<PluginDescription> types;
        for (auto* format : fm.getFormats())
            if (format->fileMightContainThisPluginType (target))
                format->findAllTypesForFile (types, target);
        if (types.isEmpty()) { std::cerr << "no plugin found in " << target << std::endl; return 1; }

        const double sr = argValue (args, "--sr", "48000").getDoubleValue();
        const int block = argValue (args, "--block", "512").getIntValue();
        int failures = 0;
        auto check = [&failures] (bool ok, const String& what) {
            std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
            if (! ok) ++failures;
        };

        String error;
        auto plugin = fm.createPluginInstance (*types[0], sr, block, error);
        check (plugin != nullptr, "instantiate " + types[0]->pluginFormatName + " '" + types[0]->name + "' " + error);
        if (plugin == nullptr) return 1;

        auto findParam = [&] (AudioPluginInstance& p, const String& name) -> AudioProcessorParameter* {
            for (auto* prm : p.getParameters())
                if (prm->getName (64) == name) return prm;
            return nullptr;
        };
        auto setParam = [&] (AudioPluginInstance& p, const String& name, const String& text) {
            if (auto* prm = findParam (p, name))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->getValueForText (text));
                prm->endChangeGesture();
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                return prm->getCurrentValueAsText();
            }
            return String ("<missing>");
        };

        check (plugin->getParameters().size() >= 40, String (plugin->getParameters().size()) + " parameters exposed");
        check (plugin->getLatencySamples() == 0, "latency " + String (plugin->getLatencySamples()) + " samples");
        check (plugin->getTotalNumOutputChannels() == 2, "stereo output");

        // like a DAW doing an offline bounce: prepare, flag offline, keep the message loop running
        auto pump = [] { MessageManager::getInstance()->runDispatchLoopUntil (100); };
        auto setOffline = [] (AudioPluginInstance& p) {
            p.setNonRealtime (true);
            // JUCE's AU host does not forward this; Logic & co. set the property for bounces
            if (auto* au = p.getAudioUnitClient())
                test::setAudioUnitOfflineRender ((void*) au->getAudioUnitHandle(), true);
        };
        plugin->prepareToPlay (sr, block);
        setOffline (*plugin);
        pump();
        std::cout << "  Mix = " << setParam (*plugin, "Mix", "100") << ", Mod Depth = " << setParam (*plugin, "Mod Depth", "0") << std::endl;

        auto renderImpulse = [&] (AudioPluginInstance& p, double seconds) {
            const int total = (int) (seconds * sr);
            AudioBuffer<float> buf (std::max (2, p.getTotalNumInputChannels()), total);
            buf.clear();
            buf.setSample (0, 0, 1.0f);
            buf.setSample (1, 0, 1.0f);
            MidiBuffer midi;
            for (int pos = 0; pos < total; pos += block)
            {
                AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), pos, std::min (block, total - pos));
                p.processBlock (view, midi);
            }
            std::array<std::vector<float>, 4> ch;
            ch[0].assign (buf.getReadPointer (0), buf.getReadPointer (0) + total);
            ch[3].assign (buf.getReadPointer (1), buf.getReadPointer (1) + total);
            return ch;
        };
        auto lateRT = [&] (std::array<std::vector<float>, 4> ch) {
            for (auto* c : { &ch[0], &ch[3] })
                for (int i = 0; i < std::min ((int) c->size(), (int) (0.12 * sr)); ++i) (*c)[(size_t) i] = 0.0f;
            return estimateRT ({ &ch[0], &ch[3] }, sr, 500.0f, 2000.0f);
        };

        // let the first IR settle (silence), then measure the impulse response through the plugin
        renderImpulse (*plugin, 0.5);
        auto ir1 = renderImpulse (*plugin, 6.0);
        const double e1 = test::energy (ir1[0]) + test::energy (ir1[3]);
        const float rt1 = lateRT (ir1);
        check (e1 > 1e-3, "reverb present (IRs found by the plugin), energy " + String (e1, 3));
        std::cout << "  native decay of the loaded preset: " << rt1 << " s (decay param "
                  << (findParam (*plugin, "Decay") ? findParam (*plugin, "Decay")->getCurrentValueAsText() : String ("?")) << ")" << std::endl;

        std::cout << "  Decay = " << setParam (*plugin, "Decay", "5") << std::endl;
        auto ir2 = renderImpulse (*plugin, 9.0);
        const float rt2 = lateRT (ir2);
        check (std::abs (rt2 - 5.0f) < 1.0f, "host parameter change rebuilds the reverb: measured RT " + String (rt2, 2) + " s (target 5)");

        // state round trip into a second instance
        MemoryBlock state;
        plugin->getStateInformation (state);
        auto second = fm.createPluginInstance (*types[0], sr, block, error);
        check (second != nullptr, "second instance");
        if (second != nullptr)
        {
            second->setStateInformation (state.getData(), (int) state.getSize());
            second->prepareToPlay (sr, block);
            setOffline (*second);
            pump();
            renderImpulse (*second, 0.5);
            auto ir3 = renderImpulse (*second, 9.0);
            const float rt3 = lateRT (ir3);
            check (std::abs (rt3 - rt2) < 0.25f, "restored state renders the same reverb (" + String (rt3, 2) + " s)");
            check (findParam (*second, "Decay") != nullptr && findParam (*second, "Decay")->getCurrentValueAsText() == findParam (*plugin, "Decay")->getCurrentValueAsText(),
                   "restored Decay parameter");
            second->releaseResources();
        }

        if (hasArg (args, "--wav"))
        {
            AudioBuffer<float> out (2, (int) ir2[0].size());
            out.copyFrom (0, 0, ir2[0].data(), out.getNumSamples());
            out.copyFrom (1, 0, ir2[3].data(), out.getNumSamples());
            test::writeWav (fileArg (args, "--wav"), out, sr);
        }

        // the hosted editor is a native view (cannot be snapshotted); just make sure it opens and closes
        if (plugin->hasEditor())
        {
            std::unique_ptr<AudioProcessorEditor> ed (plugin->createEditorAndMakeActive());
            check (ed != nullptr, "editor opens (" + String (ed ? ed->getWidth() : 0) + "x" + String (ed ? ed->getHeight() : 0) + ")");
            MessageManager::getInstance()->runDispatchLoopUntil (300);
        }
        plugin->releaseResources();
        std::cout << (failures == 0 ? "HOST TEST PASSED" : "HOST TEST FAILED") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    /** Runs the installer's extraction on a local copy of the archive and scans the result. */
    int extractArchive (const ArgumentList& args)
    {
        const File zip = fileArg (args, "--zip"), dest = fileArg (args, "--out");
        String error;
        const auto t0 = Time::getMillisecondCounterHiRes();
        const bool ok = IRInstaller::extractArchive (zip, dest, {}, error);
        IRLibrary lib;
        lib.scan (dest);
        const int sam = lib.count (IROrigin::Combined), ls = lib.count (IROrigin::Split);
        std::cout << (ok ? "extracted" : "FAILED: " + error) << " in " << (Time::getMillisecondCounterHiRes() - t0) / 1000.0
                  << " s, " << sam << " combined and " << ls << " split presets found in " << dest.getFullPathName() << std::endl;
        return ok && (sam == 134 || ls == 221) ? 0 : 1;
    }

    /** Hashes of designed responses: synthetic sources (a response in one file, a stereo one, a decay with two
        slopes, a split capture with takes) over a matrix of settings. Every sample of every part goes in, so the same
        lines before and after a change of the designer mean that no response has changed. */
    int fingerprint (const ArgumentList& args)
    {
        const double sr = argValue (args, "--sr", "48000").getDoubleValue();
        auto hashOf = [] (const BuiltIR& ir) {
            uint64 h = 1469598103934665603ULL; // FNV-1a
            auto add = [&h] (const void* data, size_t bytes) {
                const auto* b = static_cast<const uint8*> (data);
                for (size_t i = 0; i < bytes; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
            };
            auto addVector = [&add] (const std::vector<float>& v) {
                const auto n = (uint64) v.size();
                add (&n, sizeof (n));
                if (n > 0) add (v.data(), v.size() * sizeof (float));
            };
            for (auto* part : { &ir.ch, &ir.late, &ir.variant })
                for (auto& c : *part) addVector (c);
            for (auto* takes : { &ir.earlyTakes, &ir.lateTakes })
                for (auto& take : *takes)
                    for (auto& c : take) addVector (c);
            for (auto* v : { &ir.envelopeDb, &ir.nativeEnvelopeDb, &ir.rtFreqs, &ir.rtNative, &ir.rtTarget,
                             &ir.earlyTakeRho, &ir.lateTakeRho })
                addVector (*v);
            for (float v : { ir.nativeRT, ir.targetRT, ir.splitSeconds, ir.seconds, ir.midEnergy, ir.midEnergyDiagonal, ir.onsetMs })
                add (&v, sizeof (v));
            return String::toHexString ((int64) h).paddedLeft ('0', 16);
        };

        // the sources
        auto combined = makeSourceIR (test::syntheticIR (1.6f, 2.5f, sr, 21), true, sr, sr, "combined");
        auto pattern = makeSourceIR (test::syntheticIR (0.5f, 1.0f, sr, 99, 0.03f), true, sr, sr, "pattern");
        auto stereoChannels = test::syntheticIR (0.9f, 1.4f, sr, 22);
        stereoChannels[1].clear();
        stereoChannels[2].clear();
        auto stereo = makeSourceIR (stereoChannels, false, sr, sr, "stereo");
        auto twoSlopeChannels = test::syntheticIR (0.7f, 3.0f, sr, 23);
        {
            const auto slow = test::syntheticIR (2.4f, 3.0f, sr, 24);
            for (size_t c = 0; c < 4; ++c)
                for (size_t i = 0; i < twoSlopeChannels[c].size(); ++i) twoSlopeChannels[c][i] += 0.1f * slow[c][i];
        }
        auto twoSlopes = makeSourceIR (twoSlopeChannels, true, sr, sr, "two slopes");

        // a split capture: reflections and a late reverb that builds up, recorded on their own, with other takes
        auto reflections = [&] (int seed) {
            std::array<std::vector<float>, 4> ch;
            Random rng (seed);
            const int len = (int) (3.0 * sr), onset = (int) (0.01 * sr);
            for (auto& c : ch)
            {
                c.assign ((size_t) len, 0.0f);
                for (int k = 0; k < 8; ++k)
                    c[(size_t) (onset + rng.nextInt ((int) (0.06 * sr)))] += 0.8f * (rng.nextFloat() - 0.5f);
            }
            return ch;
        };
        auto lateReverb = [&] (int seed) {
            auto ch = test::syntheticIR (1.2f, 3.0f, sr, seed);
            const int onset = (int) (0.01 * sr);
            for (auto& c : ch)
                for (int i = onset; i < (int) c.size(); ++i)
                    c[(size_t) i] *= 0.2f * (float) (1.0 - std::exp (-(i - onset) / (0.03 * sr)));
            return ch;
        };
        auto split = makeSourceIR (reflections (31), lateReverb (32), lateReverb (33), true, sr, sr, "split");
        {
            // the other takes at the scale of the blocks, as loadSourceTakes hands them over
            const auto second = makeSourceIR (reflections (34), lateReverb (35), {}, true, sr, sr, "take 2");
            const auto third = makeSourceIR (reflections (36), lateReverb (37), {}, true, sr, sr, "take 3");
            split->earlyTakes = { second->earlyBlock, third->earlyBlock };
            split->lateTakes = { second->lateBlock, third->lateBlock };
            split->takesLoaded = true;
        }

        struct Case { String name; const SourceIR* src; const SourceIR* pattern; DesignParams p; };
        std::vector<Case> cases;
        auto add = [&] (const String& name, const SourceIR& src, std::function<void (DesignParams&)> set, const SourceIR* pat = nullptr) {
            DesignParams p;
            p.decay = src.nativeRT;
            if (set) set (p);
            cases.push_back ({ name, &src, pat, p });
        };
        for (auto* src : { combined.get(), stereo.get(), twoSlopes.get(), split.get() })
        {
            const String n = src->name;
            const float rt = src->nativeRT;
            add (n + " neutral", *src, nullptr);
            for (float factor : { 0.4f, 0.7f, 1.5f, 3.0f })
                for (auto mode : { DecayMode::Sabine, DecayMode::Proportional, DecayMode::ConstantLevel, DecayMode::Stretch, DecayMode::Original })
                    add (n + " x" + String (factor) + " mode " + String ((int) mode), *src,
                         [=] (DesignParams& p) { p.decay = rt * factor; p.decayMode = mode; });
            add (n + " multi-band", *src, [=] (DesignParams& p) { p.lowMult = 2.5f; p.lowFreq = 200.0f; p.highMult = 0.4f; p.highFreq = 3000.0f; });
            add (n + " levels, roll-offs, vlf", *src, [=] (DesignParams& p) { p.decay = rt * 1.3f; p.earlyDb = -3.0f; p.lateDb = 2.0f; p.vlfDb = -9.0f;
                                                                              p.erRolloffHz = 7000.0f; p.lateRolloffHz = 5000.0f; });
            add (n + " smooth", *src, [=] (DesignParams& p) { p.smooth = 1.0f; });
            add (n + " smooth, longer", *src, [=] (DesignParams& p) { p.decay = rt * 2.0f; p.smooth = 0.6f; });
            add (n + " pre-delay, delay, offset", *src, [=] (DesignParams& p) { p.decay = rt * 0.8f; p.predelayMs = 40.0f; p.delayMs = 180.0f;
                                                                                p.delayDb = -9.0f; p.offsetMs = 12.0f; });
            add (n + " negative offset", *src, [=] (DesignParams& p) { p.decay = rt * 1.2f; p.offsetMs = -8.0f; });
            add (n + " pattern", *src, [=] (DesignParams& p) { p.decay = rt * 1.4f; }, pattern.get());
            add (n + " pattern, shorter", *src, [=] (DesignParams& p) { p.decay = rt * 0.6f; }, pattern.get());
            add (n + " split late", *src, [=] (DesignParams& p) { p.decay = rt * 1.7f; p.splitLate = true; });
            add (n + " split late, variant", *src, [=] (DesignParams& p) { p.decay = rt * 0.6f; p.splitLate = p.lateVariant = true; });
            add (n + " split late, variant, neutral", *src, [=] (DesignParams& p) { p.splitLate = p.lateVariant = true; });
            add (n + " long", *src, [=] (DesignParams& p) { p.decay = 30.0f; p.decayMode = DecayMode::Proportional; p.lowMult = 4.0f; p.lowFreq = 100.0f;
                                                             p.predelayMs = 300.0f; p.delayMs = 700.0f; p.delayDb = -6.0f; });
        }
        for (auto takes : { TakeMode::Late, TakeMode::EarlyAndLate })
        {
            const float rt = split->nativeRT;
            const String n = "split takes " + String ((int) takes);
            add (n, *split, [=] (DesignParams& p) { p.splitLate = true; p.takes = takes; });
            add (n + " longer", *split, [=] (DesignParams& p) { p.decay = rt * 2.2f; p.splitLate = p.lateVariant = true; p.takes = takes; });
            add (n + " shorter, late offset", *split, [=] (DesignParams& p) { p.decay = rt * 0.5f; p.splitLate = true; p.takes = takes; p.lateOffsetMs = -30.0f; });
            add (n + " stretch, late offset", *split, [=] (DesignParams& p) { p.decay = rt * 1.6f; p.decayMode = DecayMode::Stretch; p.splitLate = true;
                                                                               p.takes = takes; p.lateOffsetMs = 60.0f; });
            add (n + " pattern", *split, [=] (DesignParams& p) { p.decay = rt * 1.3f; p.splitLate = true; p.takes = takes; }, combined.get());
        }

        for (auto& c : cases)
        {
            const auto ir = designIR (*c.src, c.pattern, c.p);
            std::cout << hashOf (*ir) << "  " << String (ir->length() / sr, 3).paddedLeft (' ', 7) << " s  " << c.name << std::endl;
        }
        std::cout << cases.size() << " cases" << std::endl;
        return 0;
    }

    /** How well DECAY keeps the shape of a preset's decay: per third octave band, the level of the designed response at
        time t against that of the recording at t / f (f: the band's target over its native reverb time), both from the
        designer's own analysis (STFT frames, no narrow filters that ring on their own), the mean offset removed. 0 dB
        is the recording stretched in time. Compared from 0.15 s on in both (after the early part), up to where the
        recording reaches its floor or fade, or has decayed by 50 dB. Prints the RMS per band over the library. */
    int shapeError (const ArgumentList& args)
    {
        SharedResourcePointer<SharedState> shared;
        if (test::irFolder().isDirectory()) shared->setIRFolder (test::irFolder());
        const double sr = 48000.0;
        const float factor = argValue (args, "--factor", "0.5").getFloatValue();
        const auto mode = (DecayMode) argValue (args, "--mode", "0").getIntValue();
        const String only = argValue (args, "--preset");
        const bool verbose = hasArg (args, "--verbose");
        // --valid: only up to where the analysis follows the recording (the end of the valid range, where the level
        // still follows the decay line; after it the synthesis carries on on the line)
        const bool validOnly = hasArg (args, "--valid");

        struct Result { String id; std::vector<float> rms, rmsSlow; int shaped; };
        std::vector<Result> results;
        std::vector<float> freqs;
        for (int i = 0; i < shared->getNumPresets(); ++i)
        {
            IRPreset preset;
            shared->getPresetAt (i, preset);
            if (only.isNotEmpty() && preset.id != only) continue;
            String err;
            auto src = loadSourceIR (preset, sr, err);
            if (src == nullptr) { std::cerr << preset.id << ": " << err << std::endl; continue; }
            DesignParams p;
            p.decay = src->nativeRT * factor;
            p.decayMode = mode;
            const auto ir = designIR (*src, nullptr, p);
            const auto built = makeSourceIR (ir->ch, src->trueStereo, sr, sr, "built");
            freqs = src->bandFreq;
            const int hop = 512;

            Result r { preset.id, {}, {}, 0 };
            for (auto& d : src->bandShapeDb) r.shaped += d.empty() ? 0 : 1;
            for (size_t b = 0; b < src->bandFreq.size(); ++b)
            {
                const float f = ir->rtTarget[b] / src->bandRT[b];
                const auto& ls = src->bandLevelDb[b];
                const auto& lo = built->bandLevelDb[b];
                std::vector<double> ds;
                const double t0 = std::max (0.15, 0.15 / f);
                const int m0 = (int) std::ceil ((src->onset + t0 * sr) / hop);
                const float startDb = m0 < (int) ls.size() ? ls[(size_t) m0] : 0.0f;
                for (int m = m0; m < (int) ls.size(); ++m)
                {
                    const double tau = (m * hop - src->onset) / sr;
                    if (tau > (validOnly ? src->bandValidEnd[b] : src->bandValidEndRecorded[b]) || ls[(size_t) m] < startDb - 50.0f) break;
                    const double x = (built->onset + f * tau * sr) / hop;
                    const int j = (int) x;
                    if (j + 1 >= (int) lo.size()) break;
                    const double out = lo[(size_t) j] + (x - j) * (lo[(size_t) j + 1] - lo[(size_t) j]);
                    ds.push_back (out - ls[(size_t) m]);
                }
                if (ds.size() < 8) { r.rms.push_back (NAN); r.rmsSlow.push_back (NAN); continue; }
                // without the mean; and the slow part alone (smoothed over a quarter of the band's reverb time): the
                // fine structure of a narrow band is random and stays at its time, the shape of the decay does not
                const double n = (double) ds.size();
                const double mean = std::accumulate (ds.begin(), ds.end(), 0.0) / n;
                const int w = std::max (1, (int) std::lround (0.125 * src->bandRT[b] * sr / hop));
                double e = 0, es = 0;
                for (size_t q = 0; q < ds.size(); ++q)
                {
                    double acc = 0.0; int cnt = 0;
                    for (int j = std::max (0, (int) q - w); j <= std::min ((int) ds.size() - 1, (int) q + w); ++j) { acc += ds[(size_t) j]; ++cnt; }
                    e += (ds[q] - mean) * (ds[q] - mean);
                    es += (acc / cnt - mean) * (acc / cnt - mean);
                }
                r.rms.push_back ((float) std::sqrt (e / n));
                r.rmsSlow.push_back ((float) std::sqrt (es / n));
            }
            if (verbose)
            {
                String line = preset.id.paddedRight (' ', 34) + String (r.shaped).paddedLeft (' ', 3);
                for (size_t b = 0; b < r.rms.size(); ++b)
                    line << (std::isfinite (r.rms[b]) ? String (r.rms[b], 1) : String ("-")).paddedLeft (' ', 5);
                std::cout << line << std::endl;
            }
            results.push_back (std::move (r));
        }

        auto quantile = [] (std::vector<float> v, float q) {
            v.erase (std::remove_if (v.begin(), v.end(), [] (float x) { return ! std::isfinite (x); }), v.end());
            if (v.empty()) return NAN;
            std::sort (v.begin(), v.end());
            return v[(size_t) std::min ((double) v.size() - 1, std::floor (q * (double) v.size()))];
        };
        std::cout << "shape error at x" << factor << " mode " << (int) mode << ", " << results.size()
                  << " presets: RMS dB per band (median / 90 % / max), then of the slow part (median / 90 %)" << std::endl;
        double sumMedian = 0.0, sumP90 = 0.0, sumSlowMedian = 0.0, sumSlowP90 = 0.0;
        int counted = 0;
        for (size_t b = 0; b < freqs.size(); ++b)
        {
            std::vector<float> v, vl;
            for (auto& r : results) { v.push_back (r.rms[b]); vl.push_back (r.rmsSlow[b]); }
            const float med = quantile (v, 0.5f), p90 = quantile (v, 0.9f), mx = quantile (v, 1.0f);
            std::cout << "  " << String (roundToInt (freqs[b])).paddedLeft (' ', 6) << " Hz  "
                      << String (med, 2).paddedLeft (' ', 6) << String (p90, 2).paddedLeft (' ', 7) << String (mx, 2).paddedLeft (' ', 7)
                      << "    " << String (quantile (vl, 0.5f), 2).paddedLeft (' ', 6) << String (quantile (vl, 0.9f), 2).paddedLeft (' ', 7)
                      << std::endl;
            if (freqs[b] >= 45.0f && freqs[b] <= 8500.0f && std::isfinite (med))
            {
                sumMedian += med; sumP90 += p90; ++counted;
                sumSlowMedian += quantile (vl, 0.5f); sumSlowP90 += quantile (vl, 0.9f);
            }
        }
        int shapedPresets = 0, shapedBands = 0;
        for (auto& r : results) { shapedPresets += r.shaped > 0 ? 1 : 0; shapedBands += r.shaped; }
        std::cout << "  presets with a shaped decay: " << shapedPresets << " (" << shapedBands << " bands)" << std::endl;
        std::cout << "  mean over 50 Hz .. 8 kHz: median " << String (sumMedian / std::max (1, counted), 2)
                  << " dB, 90 % " << String (sumP90 / std::max (1, counted), 2) << " dB; slow part: median "
                  << String (sumSlowMedian / std::max (1, counted), 2) << " dB, 90 % " << String (sumSlowP90 / std::max (1, counted), 2)
                  << " dB" << std::endl;

        // the presets with the largest error (mean over the bands 50 Hz .. 8 kHz)
        std::vector<std::pair<float, String>> worst;
        for (auto& r : results)
        {
            double s = 0.0; int c = 0;
            for (size_t b = 0; b < freqs.size(); ++b)
                if (freqs[b] >= 45.0f && freqs[b] <= 8500.0f && std::isfinite (r.rms[b])) { s += r.rms[b]; ++c; }
            worst.push_back ({ c > 0 ? (float) (s / c) : 0.0f, r.id });
        }
        std::sort (worst.begin(), worst.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        for (size_t k = 0; k < std::min<size_t> (8, worst.size()); ++k)
            std::cout << "  " << worst[k].second.paddedRight (' ', 34) << String (worst[k].first, 2) << " dB" << std::endl;
        return 0;
    }

    int listPresets (const ArgumentList&)
    {
        SharedResourcePointer<SharedState> shared;
        if (test::irFolder().isDirectory()) shared->setIRFolder (test::irFolder());
        if (test::splitFolder().isDirectory()) shared->setSplitFolder (test::splitFolder());
        for (int i = 0; i < shared->getNumPresets(); ++i)
        {
            IRPreset p;
            shared->getPresetAt (i, p);
            String err;
            auto src = loadSourceIR (p, 48000.0, err);
            std::cout << p.id.paddedRight (' ', 34) << " "
                      << (src ? String (src->nativeRT, 2) + " s" : "ERROR " + err) << std::endl;
        }
        return 0;
    }
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    ArgumentList args (argc, argv);

    // isolated settings so tests never touch the user's configuration
    SharedState::settingsFileOverride() = File::getSpecialLocation (File::tempDirectory)
                                              .getChildFile ("concert7-dev-settings.xml");
    SharedState::settingsFileOverride().deleteFile();
    SharedState::installFolderOverride() = File::getSpecialLocation (File::tempDirectory).getChildFile ("concert7-dev-install");
    SharedState::installFolderOverride().deleteRecursively();
    SharedState::getDefaultSplitInstallFolder().deleteRecursively();
    SharedState::searchTypicalPlaces() = false;
    UserPresetLibrary::folderOverride() = File::getSpecialLocation (File::tempDirectory).getChildFile ("concert7-dev-presets");
    UserPresetLibrary::folderOverride().deleteRecursively();

    // the decay's shape as up to 0.2.1-beta, to compare (see setDecayShapeEnabled)
    if (hasArg (args, "--no-shape"))
        setDecayShapeEnabled (false);

    String irs = argValue (args, "--irs", SystemStats::getEnvironmentVariable ("CONCERT7_IR_DIR", {}));
    if (irs.isNotEmpty())
        test::irFolder() = File::getCurrentWorkingDirectory().getChildFile (irs);
    if (irs.isNotEmpty() && ! test::irFolder().isDirectory())
        std::cerr << "IR folder not found: " << irs << std::endl;
    const String ls = argValue (args, "--split", SystemStats::getEnvironmentVariable ("CONCERT7_SPLIT_DIR", {}));
    if (ls.isNotEmpty())
        test::splitFolder() = File::getCurrentWorkingDirectory().getChildFile (ls);
    if (ls.isNotEmpty() && ! test::splitFolder().isDirectory())
        std::cerr << "Split captures folder not found: " << ls << std::endl;

    const String command = args.size() > 0 ? args[0].text : "test";
    int result = 0;
    if (command == "test")          result = runTests (args);
    else if (command == "render")   result = render (args);
    else if (command == "ir")       result = exportIR (args);
    else if (command == "snapshot") result = snapshot (args);
    else if (command == "bench")    result = bench (args);
    else if (command == "presets")  result = listPresets (args);
    else if (command == "analyze")  result = analyzePreset (args);
    else if (command == "fingerprint") result = fingerprint (args);
    else if (command == "shape")    result = shapeError (args);
    else if (command == "extract")  result = extractArchive (args);
    else if (command == "host")     result = hostPlugin (args);
    else
    {
        std::cout << "usage: concert7-dev test|render|ir|snapshot|bench|presets|analyze|fingerprint|shape [options] (see tests/DevMain.cpp)" << std::endl;
        result = 2;
    }
    DeletedAtShutdown::deleteAll();
    return result;
}
