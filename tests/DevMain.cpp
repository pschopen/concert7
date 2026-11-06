// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// concert7-dev: developer tool for testing the plugin without a DAW.
//
//   concert7-dev test     [--irs DIR] [--category dsp|library|processor] [--seed N]
//   concert7-dev render   --in IN.wav --out OUT.wav [--irs DIR] [--preset ID] [--set id=value ...]
//                         [--block N] [--tail SECONDS] [--sr RATE]
//   concert7-dev ir       --out IR.wav [--preset ID] [--set id=value ...] [--sr RATE]
//   concert7-dev snapshot --out UI.png [--tab 0..4] [--welcome] [--preset ID] [--scale S] [--keep decay,output]
//                         [--browser SEARCH]
//   concert7-dev bench    [--preset ID] [--seconds S] [--block N] [--set ...]
//   concert7-dev presets  [--irs DIR]            (lists presets with their analysed reverb times)
//   concert7-dev analyze  --preset ID [--sr RATE] (per band analysis of one preset)

#include "TestHelpers.h"
#include <iostream>

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

    void applySettings (Concert7Processor& proc, const ArgumentList& args)
    {
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i].text != "--set" || i + 1 >= args.size()) continue;
            const auto kv = args[i + 1].text;
            const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto value = kv.fromFirstOccurrenceOf ("=", false, false).trim().getFloatValue();
            if (auto* p = proc.getAPVTS().getParameter (id))
            {
                p->setValueNotifyingHost (p->convertTo0to1 (value));
                MessageManager::getInstance()->runDispatchLoopUntil (10); // e.g. distance mode sets its start
                std::cout << "  " << id << " = " << p->getCurrentValueAsText() << std::endl;
            }
            else
                std::cerr << "unknown parameter " << id << std::endl;
        }
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

    /** Creates a processor with test settings, an IR folder and an optional preset. */
    std::unique_ptr<Concert7Processor> makeProcessor (const ArgumentList& args, double sr, int block, bool userPreset = true)
    {
        auto proc = std::make_unique<Concert7Processor>();
        if (test::irFolder().isDirectory() && proc->getShared().getIRFolder() != test::irFolder())
            proc->getShared().setIRFolder (test::irFolder());
        if (hasArg (args, "--preset"))
            proc->selectPreset (argValue (args, "--preset"), userPreset);
        proc->prepareToPlay (sr, block);
        settle (*proc, block);
        applySettings (*proc, args);
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
        const double sr = 48000.0;
        const int block = argValue (args, "--block", "128").getIntValue();
        const double seconds = argValue (args, "--seconds", "20").getDoubleValue();
        auto proc = makeProcessor (args, sr, block);
        const int total = (int) (seconds * sr);
        AudioBuffer<float> buf (2, block);
        MidiBuffer midi;
        Random rng (2);
        double worst = 0.0;
        const auto t0 = Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += block)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, rng.nextFloat() - 0.5f);
            const auto b0 = Time::getMillisecondCounterHiRes();
            proc->processBlock (buf, midi);
            worst = std::max (worst, Time::getMillisecondCounterHiRes() - b0);
        }
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        const double blockMs = block / sr * 1000.0;
        std::cout << "IR length " << proc->getCurrentIR()->seconds << " s, block " << block << "\n"
                  << "average CPU " << 100.0 * (ms / 1000.0) / seconds << " % of one core, worst block "
                  << worst << " ms (" << 100.0 * worst / blockMs << " % of the block period)" << std::endl;
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
        std::cout << (ok ? "extracted" : "FAILED: " + error) << " in " << (Time::getMillisecondCounterHiRes() - t0) / 1000.0
                  << " s, " << lib.numBricastiPresets() << " Bricasti presets found in " << dest.getFullPathName() << std::endl;
        return ok && lib.numBricastiPresets() == 134 ? 0 : 1;
    }

    int listPresets (const ArgumentList&)
    {
        SharedResourcePointer<SharedState> shared;
        if (test::irFolder().isDirectory()) shared->setIRFolder (test::irFolder());
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
    UserPresetLibrary::folderOverride() = File::getSpecialLocation (File::tempDirectory).getChildFile ("concert7-dev-presets");
    UserPresetLibrary::folderOverride().deleteRecursively();

    String irs = argValue (args, "--irs", SystemStats::getEnvironmentVariable ("CONCERT7_IR_DIR", {}));
    if (irs.isNotEmpty())
        test::irFolder() = File::getCurrentWorkingDirectory().getChildFile (irs);
    if (irs.isNotEmpty() && ! test::irFolder().isDirectory())
        std::cerr << "IR folder not found: " << irs << std::endl;

    const String command = args.size() > 0 ? args[0].text : "test";
    int result = 0;
    if (command == "test")          result = runTests (args);
    else if (command == "render")   result = render (args);
    else if (command == "ir")       result = exportIR (args);
    else if (command == "snapshot") result = snapshot (args);
    else if (command == "bench")    result = bench (args);
    else if (command == "presets")  result = listPresets (args);
    else if (command == "analyze")  result = analyzePreset (args);
    else if (command == "extract")  result = extractArchive (args);
    else if (command == "host")     result = hostPlugin (args);
    else
    {
        std::cout << "usage: concert7-dev test|render|ir|snapshot|bench|presets [options] (see tests/DevMain.cpp)" << std::endl;
        result = 2;
    }
    DeletedAtShutdown::deleteAll();
    return result;
}
