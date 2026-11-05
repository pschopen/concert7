// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "PluginEditor.h"

namespace concert7
{
using namespace juce;

namespace
{
    constexpr int kFaceHeight = 172;
    const char* const kTabNames[] = { "REVERB", "DECAY", "EQ", "EXTRAS" };
}

//==============================================================================
MainPanel::MainPanel (Concert7Processor& p)
    : processor (p), state (p.getAPVTS()), decayView (p.getAPVTS()), eqView (p.getAPVTS(), p)
{
    setLookAndFeel (&lnf);

    // ---- faceplate
    display.onClick = [this] {
        if (processor.getShared().hasLibrary()) showPresetMenu ({}, &display);
        else showWelcome (true);
    };
    display.setTooltip ("Click to browse the presets");
    addAndMakeVisible (display);
    for (auto* b : { &prevButton, &nextButton, &browseButton })
    {
        b->getProperties().set ("fontHeight", 12.0f);
        addAndMakeVisible (b);
    }
    prevButton.onClick = [this] { processor.selectPresetByOffset (-1); };
    nextButton.onClick = [this] { processor.selectPresetByOffset (1); };
    browseButton.onClick = [this] { showPresetMenu ({}, &browseButton); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");

    // ---- main controls
    addKnob (knobs, *this, ParamID::predelay, "PRE-DELAY");
    addKnob (knobs, *this, ParamID::decay, "DECAY");
    addKnob (knobs, *this, ParamID::early, "EARLY");
    addKnob (knobs, *this, ParamID::late, "LATE");
    addKnob (knobs, *this, ParamID::mix, "MIX");
    addKnob (knobs, *this, ParamID::gain, "GAIN", true);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    // ---- tabs
    for (int i = 0; i < 4; ++i)
    {
        auto& b = tabButtons[(size_t) i];
        b.setButtonText (kTabNames[i]);
        b.getProperties().set ("tab", true);
        b.setRadioGroupId (1001);
        b.setClickingTogglesState (true);
        b.onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
        addChildComponent (pages[(size_t) i]);
    }
    setupButton.onClick = [this] { showSetupMenu(); };
    addAndMakeVisible (setupButton);

    // reverb page
    auto& rev = pages[0];
    addKnob (knobs, rev, ParamID::erPattern, "PATTERN");
    addKnob (knobs, rev, ParamID::erRolloff, "ROLL-OFF");
    addKnob (knobs, rev, ParamID::lateRolloff, "ROLL-OFF");
    addKnob (knobs, rev, ParamID::vlf, "VLF");
    addKnob (knobs, rev, ParamID::delayTime, "TIME");
    addKnob (knobs, rev, ParamID::delayLevel, "LEVEL");
    predelaySync = std::make_unique<ParamToggle> (state, ParamID::predelaySync, "SYNC");
    delaySync = std::make_unique<ParamToggle> (state, ParamID::delaySync, "SYNC");
    predelayNote = std::make_unique<ParamCombo> (state, ParamID::predelayNote);
    delayNote = std::make_unique<ParamCombo> (state, ParamID::delayNote);
    crossfeed = std::make_unique<ParamCombo> (state, ParamID::crossfeed);
    for (Component* c : std::initializer_list<Component*> { predelaySync.get(), delaySync.get(), predelayNote.get(),
                                                            delayNote.get(), crossfeed.get() })
        rev.addAndMakeVisible (c);
    for (auto [label, text] : { std::pair<Label*, const char*> { &crossfeedLabel, "PROPAGATION" },
                                { &predelayNoteLabel, "PRE-DELAY NOTE" }, { &delayNoteLabel, "DELAY NOTE" } })
    {
        label->setText (text, dontSendNotification);
        label->setFont (labelFont (9.5f));
        label->setColour (Label::textColourId, Colours::textDim);
        label->setJustificationType (Justification::centredLeft);
        rev.addAndMakeVisible (label);
    }

    // decay page
    auto& dec = pages[1];
    addKnob (knobs, dec, ParamID::lowFreq, "LOW FREQ");
    addKnob (knobs, dec, ParamID::lowMult, "LOW MULT");
    addKnob (knobs, dec, ParamID::highFreq, "HIGH FREQ");
    addKnob (knobs, dec, ParamID::highMult, "HIGH MULT");
    dec.addAndMakeVisible (decayView);

    // eq page
    auto& eqp = pages[2];
    eqp.addAndMakeVisible (eqView);
    addKnob (knobs, eqp, ParamID::eqHpFreq, "FREQ");
    addKnob (knobs, eqp, ParamID::eqLowFreq, "FREQ");
    addKnob (knobs, eqp, ParamID::eqLowGain, "GAIN", true);
    addKnob (knobs, eqp, ParamID::eqLowQ, "Q");
    addKnob (knobs, eqp, ParamID::eqMidFreq, "FREQ");
    addKnob (knobs, eqp, ParamID::eqMidGain, "GAIN", true);
    addKnob (knobs, eqp, ParamID::eqMidQ, "Q");
    addKnob (knobs, eqp, ParamID::eqHighFreq, "FREQ");
    addKnob (knobs, eqp, ParamID::eqHighGain, "GAIN", true);
    addKnob (knobs, eqp, ParamID::eqHighQ, "Q");
    addKnob (knobs, eqp, ParamID::eqLpFreq, "FREQ");
    eqOn = std::make_unique<ParamToggle> (state, ParamID::eqOn, "EQ");
    eqHpOn = std::make_unique<ParamToggle> (state, ParamID::eqHpOn, "HP");
    eqLpOn = std::make_unique<ParamToggle> (state, ParamID::eqLpOn, "LP");
    eqLowType = std::make_unique<ParamCombo> (state, ParamID::eqLowType);
    eqHighType = std::make_unique<ParamCombo> (state, ParamID::eqHighType);
    for (Component* c : std::initializer_list<Component*> { eqOn.get(), eqHpOn.get(), eqLpOn.get(), eqLowType.get(), eqHighType.get() })
        eqp.addAndMakeVisible (c);

    // extras page
    auto& ext = pages[3];
    addKnob (knobs, ext, ParamID::modDepth, "DEPTH");
    addKnob (knobs, ext, ParamID::modRate, "RATE");
    addKnob (knobs, ext, ParamID::duckAmount, "AMOUNT");
    addKnob (knobs, ext, ParamID::duckThreshold, "THRESHOLD");
    addKnob (knobs, ext, ParamID::duckRelease, "RELEASE");
    addKnob (knobs, ext, ParamID::smooth, "SMOOTH");
    duckSidechain = std::make_unique<ParamToggle> (state, ParamID::duckSidechain, "SIDECHAIN");
    ext.addAndMakeVisible (*duckSidechain);

    tabButtons[0].setToggleState (true, dontSendNotification);
    showTab (0);

    processor.addChangeListener (this);
    setSize (kWidth, kHeight); // only now: resized() needs all child components
    rebuildCategoryButtons();
    for (auto& k : knobs)
        if (k->getParamID() == ParamID::decay) lastTouched = k.get();

    auto& shared = processor.getShared();
    if (! shared.isWelcomeDone() || ! shared.hasLibrary())
        showWelcome (true);

    startTimerHz (30);
}

MainPanel::~MainPanel()
{
    stopTimer();
    processor.removeChangeListener (this);
    welcome.reset();
    setLookAndFeel (nullptr);
}

Knob& MainPanel::addKnob (std::vector<std::unique_ptr<Knob>>& list, Component& parent, const char* id,
                          const String& caption, bool bipolar)
{
    auto k = std::make_unique<Knob> (state, id, caption, bipolar);
    k->onTouched = [this] (Knob& kn) { touch (kn); };
    parent.addAndMakeVisible (*k);
    list.push_back (std::move (k));
    return *list.back();
}

void MainPanel::touch (Knob& k)
{
    lastTouched = &k;
    auto* p = state.getParameter (k.getParamID());
    display.setReadout (p != nullptr ? p->getName (32) : k.getCaption(), k.getValueText());
}

//==============================================================================
void MainPanel::rebuildCategoryButtons()
{
    categories.clear();
    processor.getShared().withLibrary ([this] (const IRLibrary& lib) { categories = lib.getCategories(); });
    if (categories.isEmpty())
        categories = StringArray { "Halls", "Plates", "Rooms", "Chambers", "Ambiences", "Spaces" };

    categoryButtons.clear();
    for (auto& c : categories)
    {
        auto b = std::make_unique<TextButton> (IRLibrary::shortCategoryName (c));
        b->getProperties().set ("led", true);
        b->getProperties().set ("fontHeight", 10.5f);
        b->setTooltip ("Presets in " + c);
        b->onClick = [this, c, ptr = b.get()] { showPresetMenu (c, ptr); };
        addAndMakeVisible (*b);
        categoryButtons.push_back (std::move (b));
    }
    resized();
}

void MainPanel::showPresetMenu (const String& onlyCategory, Component* target)
{
    PopupMenu menu;
    const auto current = processor.getPresetId();
    std::map<String, PopupMenu> sub;
    StringArray order;
    processor.getShared().withLibrary ([&] (const IRLibrary& lib) {
        const auto& presets = lib.getPresets();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const auto& p = presets[i];
            if (onlyCategory.isNotEmpty() && p.category != onlyCategory) continue;
            order.addIfNotAlreadyThere (p.category);
            sub[p.category].addItem ((int) i + 1, p.displayNumber() + "  " + p.name, true, p.id == current);
        }
    });
    if (order.isEmpty())
    {
        menu.addItem (-1, "No impulse responses - open setup");
    }
    else if (onlyCategory.isNotEmpty())
    {
        menu.addSectionHeader (onlyCategory.toUpperCase());
        menu = sub[onlyCategory];
    }
    else
    {
        for (auto& c : order)
            menu.addSubMenu (c, sub[c], true, nullptr, current.startsWith (c + "/"));
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (target).withMinimumWidth (220),
                        [this, safe = SafePointer<MainPanel> (this)] (int result) {
                            if (safe == nullptr) return;
                            if (result == -1) { showWelcome (true); return; }
                            IRPreset p;
                            if (result > 0 && processor.getShared().getPresetAt (result - 1, p))
                                processor.selectPreset (p.id, true);
                        });
}

void MainPanel::showSetupMenu()
{
    auto& shared = processor.getShared();
    PopupMenu menu;
    const auto folder = shared.getIRFolder();
    menu.addSectionHeader ("IMPULSE RESPONSES");
    menu.addItem (100, folder == File() ? String ("No folder selected") : folder.getFullPathName(), false);
    menu.addItem (1, "Choose IR folder...");
    menu.addItem (2, "Install impulse responses (ZIP)...");
    menu.addItem (3, "Rescan library");
    menu.addSeparator();
    menu.addItem (4, "Hold decay when changing presets", true, shared.getHoldDecay());
    PopupMenu sizes;
    const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i)
        sizes.addItem (10 + i, String (roundToInt (scales[i] * 100.0f)) + " %", true,
                       std::abs (shared.getUIScale() - scales[i]) < 0.01f);
    menu.addSubMenu ("Interface size", sizes);
    menu.addSeparator();
    menu.addItem (5, "Reset all parameters");
    menu.addItem (6, "About Concert7...");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&setupButton).withMinimumWidth (240),
                        [this, safe = SafePointer<MainPanel> (this)] (int r) {
                            if (safe == nullptr) return;
                            auto& sh = processor.getShared();
                            if (r == 1)
                            {
                                showWelcome (true);
                                if (welcome != nullptr) welcome->chooseFolder();
                            }
                            else if (r == 2) showWelcome (true);
                            else if (r == 3) sh.rescan();
                            else if (r == 4) sh.setHoldDecay (! sh.getHoldDecay());
                            else if (r >= 10 && r < 15)
                            {
                                const float chosen[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
                                sh.setUIScale (chosen[r - 10]);
                                if (onScaleChange) onScaleChange (chosen[r - 10]);
                            }
                            else if (r == 5)
                            {
                                for (auto* prm : processor.getParameters())
                                    if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                                        if (rp->paramID != ParamID::decay)
                                            rp->setValueNotifyingHost (rp->getDefaultValue());
                                processor.selectPreset (processor.getPresetId(), true);
                            }
                            else if (r == 6)
                            {
                                AlertWindow::showMessageBoxAsync (
                                    MessageBoxIconType::NoIcon, "Concert7 " CONCERT7_VERSION,
                                    "Open source true stereo convolution reverb (GPL-3.0).\n\n"
                                    "Built on the REEV-R convolution engine by tiagolr and the FFTConvolver library by HiFi-LoFi.\n\n"
                                    "Impulse responses: Bricasti M7 library by Samplicity (download separately).\n"
                                    "Bricasti and M7 are trademarks of their respective owner; "
                                    "Concert7 is not affiliated with Bricasti or Samplicity.");
                            }
                        });
}

void MainPanel::showWelcome (bool show)
{
    if (show && welcome == nullptr)
    {
        welcome = std::make_unique<WelcomeOverlay> (processor);
        // the overlay must not delete itself from inside its own button callback
        SafePointer<MainPanel> safe (this);
        welcome->onClose = [safe] {
            MessageManager::callAsync ([safe] { if (safe != nullptr) safe->showWelcome (false); });
        };
        addAndMakeVisible (*welcome);
        welcome->setBounds (getLocalBounds());
    }
    else if (! show)
    {
        welcome.reset();
    }
}

void MainPanel::showTab (int index)
{
    currentTab = jlimit (0, 3, index);
    for (int i = 0; i < 4; ++i)
    {
        pages[(size_t) i].setVisible (i == currentTab);
        tabButtons[(size_t) i].setToggleState (i == currentTab, dontSendNotification);
    }
    repaint();
}

//==============================================================================
void MainPanel::changeListenerCallback (ChangeBroadcaster*)
{
    StringArray now;
    processor.getShared().withLibrary ([&now] (const IRLibrary& lib) { now = lib.getCategories(); });
    if (now != categories && ! now.isEmpty())
        rebuildCategoryButtons();
}

void MainPanel::timerCallback()
{
    inMeter.setLevels (processor.inMeterL.take(), processor.inMeterR.take());
    outMeter.setLevels (processor.outMeterL.take(), processor.outMeterR.take());

    const auto status = processor.getStatus();
    display.setStatus (status, processor.getShared().hasLibrary());
    if (status.loading) display.repaint();

    auto ir = processor.getCurrentIR();
    if (ir != shownIR)
    {
        shownIR = ir;
        display.setIR (ir);
        decayView.setIR (ir);
    }
    if (lastTouched != nullptr)
    {
        auto* p = state.getParameter (lastTouched->getParamID());
        display.setReadout (p != nullptr ? p->getName (32) : lastTouched->getCaption(), lastTouched->getValueText());
    }

    const auto id = processor.getPresetId();
    if (id != lastPresetId)
    {
        lastPresetId = id;
        for (size_t i = 0; i < categoryButtons.size(); ++i)
            categoryButtons[i]->setToggleState (id.startsWith (categories[(int) i] + "/"), dontSendNotification);
    }

    for (auto& k : knobs) k->repaint(); // values may change by automation
    if (currentTab == 1) decayView.repaint();

    // pre-delay / delay knobs are replaced by the note value when synced
    for (auto& k : knobs)
    {
        if (k->getParamID() == ParamID::predelay) k->setEnabled (state.getRawParameterValue (ParamID::predelaySync)->load() < 0.5f);
        if (k->getParamID() == ParamID::delayTime) k->setEnabled (state.getRawParameterValue (ParamID::delaySync)->load() < 0.5f);
    }
    predelayNote->setEnabled (predelaySync->getToggleState());
    delayNote->setEnabled (delaySync->getToggleState());
}

//==============================================================================
void MainPanel::paint (Graphics& g)
{
    g.fillAll (Colours::background);

    // faceplate
    auto face = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kFaceHeight);
    g.setGradientFill (ColourGradient (Colours::faceTop, 0.0f, 0.0f, Colours::faceBottom, 0.0f, face.getBottom(), false));
    g.fillRect (face);
    // brushed metal texture
    Random rng (7);
    for (int y = 0; y < kFaceHeight; y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.012f * rng.nextFloat()));
        g.fillRect (0.0f, (float) y, (float) kWidth, 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (juce::Colours::black);
    g.fillRect (0.0f, face.getBottom() - 2.0f, (float) kWidth, 2.0f);
    for (auto pt : { Point<float> (16, 16), Point<float> (984, 16), Point<float> (16, 156), Point<float> (984, 156) })
        drawScrew (g, pt, 6.0f);

    // logo
    drawLogo (g, 40.0f, 78.0f);
    g.setColour (Colours::textDim);
    g.setFont (labelFont (10.0f));
    g.drawText ("TRUE STEREO REVERB", 42, 88, 220, 14, Justification::left);
    g.setColour (Colours::textFaint);
    g.drawText ("OPEN SOURCE", 42, 104, 220, 14, Justification::left);

    // power LED
    const bool ok = processor.getShared().hasLibrary();
    g.setColour ((ok ? Colours::ledOn : Colours::warning).withAlpha (0.3f));
    g.fillEllipse (39.0f, 131.0f, 12.0f, 12.0f);
    g.setColour (ok ? Colours::ledOn : Colours::warning);
    g.fillEllipse (42.0f, 134.0f, 6.0f, 6.0f);
    g.setColour (Colours::textFaint);
    g.setFont (labelFont (9.0f));
    g.drawText (ok ? "READY" : "NO IRS", 56, 131, 80, 12, Justification::left);

    // main section
    drawGroup (g, mainArea.toFloat(), {});
    g.setColour (Colours::outline);
    const float sepX1 = (float) mainArea.getX() + 600.0f;
    const float sepX2 = (float) mainArea.getX() + 830.0f;
    for (float x : { sepX1, sepX2 })
        g.fillRect (x, (float) mainArea.getY() + 18.0f, 1.0f, (float) mainArea.getHeight() - 36.0f);

    // tab page
    drawGroup (g, pageArea.toFloat(), {});

    // group frames inside pages (drawn in panel coordinates)
    auto frame = [&] (Component& page, Rectangle<int> r, const String& title) {
        if (! page.isVisible()) return;
        drawGroup (g, r.translated (page.getX(), page.getY()).toFloat(), title);
    };
    if (currentTab == 0)
    {
        frame (pages[0], { 8, 8, 196, 180 }, "EARLY REFLECTIONS");
        frame (pages[0], { 212, 8, 196, 180 }, "LATE REVERB");
        frame (pages[0], { 416, 8, 300, 180 }, "DELAY");
        frame (pages[0], { 724, 8, 220, 180 }, "PRE-DELAY / ROUTING");
    }
    else if (currentTab == 1)
    {
        frame (pages[1], { 8, 8, 330, 180 }, "MULTI-BAND DECAY");
    }
    else if (currentTab == 2)
    {
        const char* names[] = { "HIGH PASS", "LOW", "MID", "HIGH", "LOW PASS" };
        for (int i = 0; i < 5; ++i)
            frame (pages[2], { 420 + i * 105, 8, 100, 180 }, names[i]);
    }
    else if (currentTab == 3)
    {
        frame (pages[3], { 8, 8, 240, 180 }, "MODULATION");
        frame (pages[3], { 256, 8, 470, 180 }, "DUCKER");
        frame (pages[3], { 734, 8, 210, 180 }, "SMOOTH");
        // gain reduction readout
        auto gr = Rectangle<float> (600.0f, 50.0f, 110.0f, 100.0f).translated ((float) pages[3].getX(), (float) pages[3].getY());
        g.setColour (Colours::lcdBack);
        g.fillRoundedRectangle (gr, 5.0f);
        const float red = jlimit (0.0f, 40.0f, processor.getDuckReductionDb());
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (gr.reduced (40.0f, 8.0f).withHeight ((gr.getHeight() - 16.0f) * red / 40.0f), 2.0f);
        g.setColour (Colours::textDim);
        g.setFont (labelFont (9.0f));
        g.drawText ("GR", gr.reduced (6.0f), Justification::topLeft);
        g.setColour (Colours::text);
        g.setFont (uiFont (11.0f));
        g.drawText ("-" + String (red, 1) + " dB", gr.reduced (6.0f), Justification::bottomLeft);
    }
}

void MainPanel::resized()
{
    // faceplate
    display.setBounds (270, 20, 470, 132);
    const int n = (int) categoryButtons.size();
    const int cols = std::max (3, (n + 1) / 2);
    const int gridX = 756, gridW = 228, bw = (gridW - (cols - 1) * 6) / cols, bh = 30;
    for (int i = 0; i < n; ++i)
        categoryButtons[(size_t) i]->setBounds (gridX + (i % cols) * (bw + 6), 22 + (i / cols) * (bh + 6), bw, bh);
    prevButton.setBounds (gridX, 112, 44, 34);
    browseButton.setBounds (gridX + 50, 112, gridW - 100, 34);
    nextButton.setBounds (gridX + gridW - 44, 112, 44, 34);

    // main knobs
    mainArea = { 12, kFaceHeight + 12, kWidth - 24, 208 };
    const int top = mainArea.getY() + 14;
    auto place = [&] (const char* id, Rectangle<int> r) {
        for (auto& k : knobs)
            if (k->getParamID() == id)
            {
                k->captionHeight = 18.0f;
                k->valueHeight = 18.0f;
                k->setBounds (r);
            }
    };
    const int x0 = mainArea.getX() + 14;
    place (ParamID::predelay, { x0, top + 20, 120, 156 });
    place (ParamID::decay, { x0 + 128, top, 168, 184 });
    place (ParamID::early, { x0 + 304, top + 20, 130, 156 });
    place (ParamID::late, { x0 + 442, top + 20, 130, 156 });
    place (ParamID::mix, { mainArea.getX() + 610, top + 20, 104, 156 });
    place (ParamID::gain, { mainArea.getX() + 718, top + 20, 104, 156 });
    inMeter.setBounds (mainArea.getX() + 852, top + 10, 46, 170);
    outMeter.setBounds (mainArea.getX() + 908, top + 10, 46, 170);

    // tabs
    const int tabY = mainArea.getBottom() + 10;
    for (int i = 0; i < 4; ++i)
        tabButtons[(size_t) i].setBounds (12 + i * 128, tabY, 124, 28);
    setupButton.setBounds (kWidth - 12 - 110, tabY, 110, 28);
    pageArea = { 12, tabY + 32, kWidth - 24, kHeight - (tabY + 32) - 10 };
    for (auto& p : pages)
        p.setBounds (pageArea.reduced (8, 0).withTrimmedTop (2));

    auto small = [&] (Component& page, const char* id, Rectangle<int> r) {
        for (auto& k : knobs)
            if (k->getParamID() == id && k->getParentComponent() == &page)
            {
                k->captionHeight = 15.0f;
                k->valueHeight = 15.0f;
                k->setBounds (r);
            }
    };

    // reverb page
    auto& rev = pages[0];
    small (rev, ParamID::erPattern, { 16, 34, 86, 120 });
    small (rev, ParamID::erRolloff, { 110, 34, 86, 120 });
    small (rev, ParamID::lateRolloff, { 220, 34, 86, 120 });
    small (rev, ParamID::vlf, { 314, 34, 86, 120 });
    small (rev, ParamID::delayTime, { 424, 34, 86, 120 });
    small (rev, ParamID::delayLevel, { 518, 34, 86, 120 });
    delaySync->setBounds (614, 46, 90, 26);
    delayNoteLabel.setBounds (614, 84, 96, 14);
    delayNote->setBounds (614, 100, 90, 24);
    predelaySync->setBounds (736, 34, 92, 26);
    predelayNoteLabel.setBounds (736, 66, 110, 14);
    predelayNote->setBounds (736, 82, 92, 24);
    crossfeedLabel.setBounds (736, 116, 120, 14);
    crossfeed->setBounds (736, 132, 196, 24);

    // decay page
    auto& dec = pages[1];
    small (dec, ParamID::lowFreq, { 16, 34, 76, 120 });
    small (dec, ParamID::lowMult, { 96, 34, 76, 120 });
    small (dec, ParamID::highFreq, { 176, 34, 76, 120 });
    small (dec, ParamID::highMult, { 256, 34, 76, 120 });
    decayView.setBounds (346, 8, dec.getWidth() - 354, 180);

    // eq page
    auto& eqp = pages[2];
    eqView.setBounds (8, 8, 404, 180);
    auto col = [&] (int i) { return 420 + i * 105; };
    eqHpOn->setBounds (col (0) + 12, 28, 76, 22);
    small (eqp, ParamID::eqHpFreq, { col (0) + 10, 58, 80, 100 });
    eqLowType->setBounds (col (1) + 8, 28, 84, 20);
    small (eqp, ParamID::eqLowFreq, { col (1) + 4, 52, 46, 66 });
    small (eqp, ParamID::eqLowGain, { col (1) + 50, 52, 46, 66 });
    small (eqp, ParamID::eqLowQ, { col (1) + 27, 118, 46, 66 });
    small (eqp, ParamID::eqMidFreq, { col (2) + 4, 52, 46, 66 });
    small (eqp, ParamID::eqMidGain, { col (2) + 50, 52, 46, 66 });
    small (eqp, ParamID::eqMidQ, { col (2) + 27, 118, 46, 66 });
    eqHighType->setBounds (col (3) + 8, 28, 84, 20);
    small (eqp, ParamID::eqHighFreq, { col (3) + 4, 52, 46, 66 });
    small (eqp, ParamID::eqHighGain, { col (3) + 50, 52, 46, 66 });
    small (eqp, ParamID::eqHighQ, { col (3) + 27, 118, 46, 66 });
    eqLpOn->setBounds (col (4) + 12, 28, 76, 22);
    small (eqp, ParamID::eqLpFreq, { col (4) + 10, 58, 80, 100 });
    eqOn->setBounds (col (4) + 12, 160, 76, 22);
    for (auto& k : knobs)
        if (k->getParentComponent() == &eqp)
        {
            k->captionHeight = 13.0f;
            k->valueHeight = 13.0f;
            k->resized();
        }

    // extras page
    auto& ext = pages[3];
    small (ext, ParamID::modDepth, { 24, 34, 100, 130 });
    small (ext, ParamID::modRate, { 132, 34, 100, 130 });
    small (ext, ParamID::duckAmount, { 270, 34, 100, 130 });
    small (ext, ParamID::duckThreshold, { 372, 34, 100, 130 });
    small (ext, ParamID::duckRelease, { 474, 34, 100, 130 });
    small (ext, ParamID::smooth, { 789, 34, 100, 130 });
    duckSidechain->setBounds (600, 160 - 8, 110, 24);

    if (welcome != nullptr)
        welcome->setBounds (getLocalBounds());
}

//==============================================================================
Concert7Editor::Concert7Editor (Concert7Processor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onScaleChange = [this] (float s) {
        setSize (roundToInt (MainPanel::kWidth * s), roundToInt (MainPanel::kHeight * s));
    };
    setResizable (true, true);
    setResizeLimits (MainPanel::kWidth * 6 / 10, MainPanel::kHeight * 6 / 10, MainPanel::kWidth * 2, MainPanel::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainPanel::kWidth / MainPanel::kHeight);
    const float s = jlimit (0.6f, 2.0f, p.getShared().getUIScale());
    setSize (roundToInt (MainPanel::kWidth * s), roundToInt (MainPanel::kHeight * s));
}

Concert7Editor::~Concert7Editor() = default;

void Concert7Editor::resized()
{
    const float s = (float) getWidth() / (float) MainPanel::kWidth;
    panel.setTransform (AffineTransform::scale (s));
    panel.setBounds (0, 0, MainPanel::kWidth, MainPanel::kHeight);
}
} // namespace concert7
