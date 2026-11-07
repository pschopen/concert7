// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "PluginEditor.h"

namespace concert7
{
using namespace juce;

namespace
{
    constexpr int kFaceHeight = 172;

    /** Space page, in page coordinates, left to right: HALL above the DECAY it gives; DISTANCE as a large knob
        with its two options stacked beside it; the DRY SIGNAL it sets, five knobs like the five on a die; the
        top view. All small knobs sit on two rows (the middle of the die between them). */
    namespace SpaceGrid
    {
        constexpr int knobW = 68, knobH = 66, bigW = 100, optionW = 72, dieW = 64;
        constexpr int hallX = 8, hallW = 2 * knobW + 20;
        constexpr int distX = hallX + hallW + 8, distW = 10 + bigW + 8 + optionW + 10;
        constexpr int dryX = distX + distW + 8, dryW = 3 * dieW + 20;
        constexpr int viewX = dryX + dryW + 8;
        constexpr int topY = 8, topH = 88, bottomY = topY + topH + 8, bottomH = 188 - bottomY;
        constexpr int row1 = 28, row2 = 116, rowMiddle = (row1 + row2) / 2;
        constexpr int hallColumn (int i) { return hallX + 10 + i * knobW; }
        constexpr int dieColumn (int i) { return dryX + 10 + i * dieW; }
    }
    const char* const kTabNames[] = { "REVERB", "DECAY", "EQ", "EXTRAS", "SPACE" };
    const char* const kTabTooltips[] = {
        "Early reflections, late reverb, delay, start of the impulse response and stereo propagation",
        "Decay time of the lows and highs, separately",
        "Equalizer for the reverb signal (the dry signal is not affected)",
        "Movement in the reverb (modulation), Smooth and ducking",
        "Set the reverb by hall size, place a dry source nearer or further away, "
        "and the tone, width and timing of the dry signal"
    };

    /** What a control does, for the end user. Knobs, toggles and combo boxes are looked up by their parameter. */
    String tooltipFor (const String& paramID)
    {
        static const std::map<String, String> tips {
            // main
            { ParamID::predelay, "Time before the late reverb starts. More pre-delay keeps vocals and instruments clear "
                                 "in front of the reverb; the early reflections stay where they are." },
            { ParamID::decay, "How long the reverb rings out (RT60). Starts at the preset's own decay. "
                              "Set by the hall model while SPACE > ON is active." },
            { ParamID::early, "Level of the early reflections - the first echoes that tell the ear how big the room is. "
                              "All the way down: off." },
            { ParamID::late, "Level of the late reverb, the dense tail. All the way down: off." },
            { ParamID::mix, "Balance between dry (0 %) and reverb (100 %). Use 100 % on a send/aux. "
                            "Set by the distance mode while it is on." },
            { ParamID::gain, "Output level after the mix." },
            // reverb
            { ParamID::erPattern, "Swaps the early reflections for those of another preset (1 = shortest ... 32 = longest). "
                                  "\"Preset\" keeps the preset's own." },
            { ParamID::erRolloff, "Darkens the early reflections. Off = full bandwidth." },
            { ParamID::lateRolloff, "Darkens the reverb tail, like a hall with soft surfaces. Off = full bandwidth." },
            { ParamID::vlf, "Level of the deep bass in the reverb (below about 120 Hz). "
                            "Turn it down if the reverb muddies the low end." },
            { ParamID::predelaySync, "Sets the pre-delay as a note value of the host tempo." },
            { ParamID::predelayNote, "Note value for the pre-delay (only with SYNC)." },
            { ParamID::irOffset, "Moves the start of the whole impulse response, up to 5 s either way. Positive: the reverb starts later. "
                                 "Negative: its beginning is cut - a negative pre-delay that thins out or removes "
                                 "the early reflections and lets the tail come in sooner." },
            { ParamID::delayTime, "Spacing of the echoes. The delay adds three repeats of the reverb - a rhythmic reverb tail." },
            { ParamID::delayLevel, "Volume of the echoes. All the way down: delay off." },
            { ParamID::delaySync, "Sets the delay time as a note value of the host tempo." },
            { ParamID::delayNote, "Note value for the delay (only with SYNC)." },
            { ParamID::crossfeed, "How the input reaches the reverb. True Stereo: each side excites the whole room, as recorded. "
                                  "No Crossfeed: left stays left, right stays right - more defined. "
                                  "Mono In: both channels summed - the source sits in the centre." },
            // decay
            { ParamID::lowFreq, "Below this frequency, LOW MULT changes the decay." },
            { ParamID::lowMult, "Decay of the lows relative to DECAY. Above 1x: warmer, longer bass. Below 1x: tighter bass." },
            { ParamID::highFreq, "Above this frequency, HIGH MULT changes the decay." },
            { ParamID::highMult, "Decay of the highs relative to DECAY. Below 1x: darker tail, like real halls. "
                                 "Above 1x: brighter, airier." },
            // eq
            { ParamID::eqOn, "Switches the whole EQ on or off. It only affects the reverb." },
            { ParamID::eqHpOn, "High-pass - removes rumble and low mud from the reverb." },
            { ParamID::eqHpFreq, "Cut-off frequency of the high-pass." },
            { ParamID::eqLowType, "Shelf raises or lowers everything below the frequency; Bell only the range around it." },
            { ParamID::eqLowFreq, "Centre or corner frequency of the low band." },
            { ParamID::eqLowGain, "Boost or cut of the low band." },
            { ParamID::eqLowQ, "Bandwidth of the low band. Low values: wide. High values: narrow." },
            { ParamID::eqMidFreq, "Centre frequency of the mid band." },
            { ParamID::eqMidGain, "Boost or cut of the mid band." },
            { ParamID::eqMidQ, "Bandwidth of the mid band. Low values: wide. High values: narrow." },
            { ParamID::eqHighType, "Shelf raises or lowers everything above the frequency; Bell only the range around it." },
            { ParamID::eqHighFreq, "Centre or corner frequency of the high band." },
            { ParamID::eqHighGain, "Boost or cut of the high band." },
            { ParamID::eqHighQ, "Bandwidth of the high band. Low values: wide. High values: narrow." },
            { ParamID::eqLpOn, "Low-pass - removes harsh highs from the reverb." },
            { ParamID::eqLpFreq, "Cut-off frequency of the low-pass." },
            // extras
            { ParamID::modMode, "Kind of movement. VIBRATO: the pitch of the whole reverb wavers. "
                                "CHORUS: only the late reverb wavers, slightly and differently left and right. "
                                "DRIFT (most natural): the late reverb keeps changing its fine structure - alive, but no pitch change. "
                                "DRIFT + CHORUS: both, like the tail of an algorithmic hall. "
                                "Early reflections stay untouched except in VIBRATO. "
                                "DRIFT needs about twice the CPU." },
            { ParamID::modDepth, "Amount of movement in the reverb: livelier, less static. 0 = off." },
            { ParamID::modRate, "Speed of the modulation." },
            { ParamID::smooth, "Evens out ringing frequencies and bumps in the tail. "
                               "Helps when a preset rings or flutters at long decay times." },
            { ParamID::duckAmount, "How far the reverb is lowered while the signal plays; it comes back in the pauses. "
                                   "Keeps vocals clear with a lot of reverb. Off = no ducking." },
            { ParamID::duckThreshold, "Input level above which ducking starts." },
            { ParamID::duckRelease, "How quickly the reverb comes back once the signal gets quiet." },
            { ParamID::duckSidechain, "Ducking follows the DAW's sidechain input instead of the plugin's own input - "
                                      "e.g. the band's reverb ducks under the lead vocal." },
            // direct
            { ParamID::directHighs, "Presence of the dry signal. Plus: closer and brighter. Minus: further away. "
                                    "Set by DISTANCE while it is on." },
            { ParamID::directAir, "Damps the highest frequencies of the dry signal, as over a long distance through air. "
                                  "Set by DISTANCE while it is on." },
            { ParamID::directWidth, "Stereo width of the dry signal. 100 % = unchanged, 0 % = mono. "
                                    "Set by DISTANCE while it is on." },
            { ParamID::directDelay, "Delays the dry signal, e.g. to align a spot microphone with a main pair. "
                                    "Set by DISTANCE while it is on." },
            { ParamID::directGap, "Delays the whole reverb, including the early reflections, against the dry signal. "
                                  "A larger gap makes the source sound closer. "
                                  "Set by DISTANCE while it is on." },
            // space
            { ParamID::hallOn, "Sets the reverb by the size of the hall and its surfaces instead of DECAY. "
                               "Switching it on or off keeps the current sound." },
            { ParamID::hallVolume, String (CharPointer_UTF8 ("Size of the hall. Bigger = longer reverb "
                                                             "(about 16 000 m\xc2\xb3 = large concert hall).")) },
            { ParamID::hallAbsorption, "How much the surfaces absorb - from stone church (very reverberant) to studio (dry)." },
            { ParamID::distOn, "Places the dry source nearer or further away in the hall; MIX and the DRY SIGNAL controls follow automatically. "
                               "Switching it on or off keeps the current sound. Use it as an insert on a dry track." },
            { ParamID::distance, "Distance from the source to the listener. Further = more reverb, less presence." },
            { ParamID::distWidth, "How wide the instrument or ensemble really is (e.g. 2 m for a piano, 15 m for an orchestra) - "
                                "not the stereo width. Further away it covers a smaller angle, so the dry signal gets "
                                "narrower. Off: the stereo image stays as it is." },
            { ParamID::distMic, "For spot microphones against a main pair: how far the spot microphone was from the source. "
                              "The dry signal is delayed by the sound's travel time from there to DISTANCE. Off: no delay." },
        };
        const auto it = tips.find (paramID);
        return it != tips.end() ? it->second : String();
    }

    String categoryTooltip (const String& category)
    {
        static const std::map<String, String> tips {
            { "Halls", "Concert halls - long, smooth reverb for orchestra, choir, piano and strings." },
            { "Plates", "Plate reverbs - dense and bright, without room reflections. Classic on vocals and snare." },
            { "Rooms", "Small and medium rooms - short, natural ambience for drums, guitars and dialogue." },
            { "Chambers", "Echo chambers - dense, warm studio reverb." },
            { "Ambiences", "Very short reverb that adds space without an audible tail." },
            { "Spaces", "Real places - churches, a scoring stage, an arena, a car park, a cavern, outdoor spaces - "
                        "and a gated reverb." },
            { "User", "Your own impulse responses from the IR folder (WAV, AIFF, FLAC)." },
        };
        const auto it = tips.find (category);
        return it != tips.end() ? it->second : "Presets in " + category;
    }
}

//==============================================================================
MainPanel::MainPanel (Concert7Processor& p)
    : processor (p), state (p.getAPVTS()), decayView (p.getAPVTS()), eqView (p.getAPVTS(), p), distanceView (p)
{
    setLookAndFeel (&lnf);

    // ---- faceplate
    display.onClick = [this] {
        if (processor.getShared().hasLibrary()) showBrowser (! browser.isVisible());
        else showWelcome (true);
    };
    display.setTooltip ("Shows the preset, the value of the last control you touched and the decay envelope. "
                        "Click to open the preset browser.");
    addAndMakeVisible (display);
    for (auto* b : { &prevButton, &nextButton, &browseButton })
    {
        b->getProperties().set ("fontHeight", 12.0f);
        addAndMakeVisible (b);
    }
    // < > step through the browser's list as it is filtered (all presets at first)
    prevButton.onClick = [this] { browser.step (-1); };
    nextButton.onClick = [this] { browser.step (1); };
    browseButton.onClick = [this] { showBrowser (! browser.isVisible()); };
    browser.onClose = [this] { showBrowser (false); };
    addChildComponent (browser);
    prevButton.setTooltip ("Previous preset (of the browser's list as it is filtered)");
    nextButton.setTooltip ("Next preset (of the browser's list as it is filtered)");
    browseButton.setTooltip ("Opens the preset browser: library presets and your own, search, favourites, tags, saving");

    // ---- main controls
    addKnob (knobs, *this, ParamID::predelay, "PRE-DELAY");
    addKnob (knobs, *this, ParamID::decay, "DECAY");
    addKnob (knobs, *this, ParamID::early, "EARLY");
    addKnob (knobs, *this, ParamID::late, "LATE");
    addKnob (knobs, *this, ParamID::mix, "MIX");
    addKnob (knobs, *this, ParamID::gain, "GAIN", true);
    inMeter.setTooltip ("Input level");
    outMeter.setTooltip ("Output level");
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    // ---- tabs
    for (int i = 0; i < kNumTabs; ++i)
    {
        auto& b = tabButtons[(size_t) i];
        b.setButtonText (kTabNames[i]);
        b.getProperties().set ("tab", true);
        b.setRadioGroupId (1001);
        b.setClickingTogglesState (true);
        b.onClick = [this, i] { showTab (i); };
        b.setTooltip (kTabTooltips[i]);
        addAndMakeVisible (b);
        addChildComponent (pages[(size_t) i]);
    }
    // undo / redo and A/B
    undoButton.onClick = [this] { processor.getHistory().undo(); updateHistoryButtons(); };
    redoButton.onClick = [this] { processor.getHistory().redo(); updateHistoryButtons(); };
    slotA.onClick = [this] { processor.selectABSlot (0); updateHistoryButtons(); };
    slotB.onClick = [this] { processor.selectABSlot (1); updateHistoryButtons(); };
    copySlot.onClick = [this] { processor.copyToOtherABSlot(); };
    slotA.setTooltip ("Settings A. A and B hold two complete settings (impulse response included) to compare; "
                      "switching keeps the other one as you left it.");
    slotB.setTooltip ("Settings B. The first time you switch to B it starts as a copy of A.");
    for (auto* b : { &undoButton, &redoButton, &slotA, &slotB, &copySlot })
    {
        b->getProperties().set ("fontHeight", 10.5f);
        addAndMakeVisible (*b);
    }
    for (auto* b : { &slotA, &slotB })
        b->getProperties().set ("led", true);
    setWantsKeyboardFocus (true); // Cmd/Ctrl+Z, Shift+Cmd+Z / Ctrl+Y
    updateHistoryButtons();

    setupButton.onClick = [this] { showSetupMenu(); };
    setupButton.setTooltip ("Impulse response library, interface size, settings kept on preset change, CPU modes, reset");
    addAndMakeVisible (setupButton);

    // reverb page
    auto& rev = pages[0];
    addKnob (knobs, rev, ParamID::erPattern, "PATTERN");
    addKnob (knobs, rev, ParamID::erRolloff, "ROLL-OFF");
    addKnob (knobs, rev, ParamID::lateRolloff, "ROLL-OFF");
    addKnob (knobs, rev, ParamID::vlf, "VLF");
    addKnob (knobs, rev, ParamID::delayTime, "TIME");
    addKnob (knobs, rev, ParamID::delayLevel, "LEVEL");
    addKnob (knobs, rev, ParamID::irOffset, "OFFSET", true);
    predelaySync = std::make_unique<ParamToggle> (state, ParamID::predelaySync, "SYNC");
    delaySync = std::make_unique<ParamToggle> (state, ParamID::delaySync, "SYNC");
    predelayNote = std::make_unique<ParamCombo> (state, ParamID::predelayNote);
    delayNote = std::make_unique<ParamCombo> (state, ParamID::delayNote);
    crossfeed = std::make_unique<ParamCombo> (state, ParamID::crossfeed);
    for (Component* c : std::initializer_list<Component*> { predelaySync.get(), delaySync.get(), predelayNote.get(),
                                                            delayNote.get(), crossfeed.get() })
        rev.addAndMakeVisible (c);
    predelaySync->setTooltip (tooltipFor (ParamID::predelaySync));
    delaySync->setTooltip (tooltipFor (ParamID::delaySync));
    predelayNote->setTooltip (tooltipFor (ParamID::predelayNote));
    delayNote->setTooltip (tooltipFor (ParamID::delayNote));
    crossfeed->setTooltip (tooltipFor (ParamID::crossfeed));
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
    decayView.setTooltip ("Reverb time over frequency. Dashed = the preset as recorded, solid = your setting.");
    dec.addAndMakeVisible (decayView);

    // eq page
    auto& eqp = pages[2];
    eqView.setTooltip ("Drag a point to change frequency and gain. Mouse wheel: Q. "
                       "Double-click: switches HP/LP on or off, or resets the gain to 0 dB.");
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
    eqOn->setTooltip (tooltipFor (ParamID::eqOn));
    eqHpOn->setTooltip (tooltipFor (ParamID::eqHpOn));
    eqLpOn->setTooltip (tooltipFor (ParamID::eqLpOn));
    eqLowType->setTooltip (tooltipFor (ParamID::eqLowType));
    eqHighType->setTooltip (tooltipFor (ParamID::eqHighType));

    // extras page
    auto& ext = pages[3];
    modMode = std::make_unique<ParamCombo> (state, ParamID::modMode);
    modMode->setTooltip (tooltipFor (ParamID::modMode));
    ext.addAndMakeVisible (*modMode);
    addKnob (knobs, ext, ParamID::modDepth, "DEPTH");
    addKnob (knobs, ext, ParamID::modRate, "RATE");
    addKnob (knobs, ext, ParamID::duckAmount, "AMOUNT");
    addKnob (knobs, ext, ParamID::duckThreshold, "THRESHOLD");
    addKnob (knobs, ext, ParamID::duckRelease, "RELEASE");
    addKnob (knobs, ext, ParamID::smooth, "SMOOTH");
    duckSidechain = std::make_unique<ParamToggle> (state, ParamID::duckSidechain, "SIDECHAIN");
    duckSidechain->setTooltip (tooltipFor (ParamID::duckSidechain));
    ext.addAndMakeVisible (*duckSidechain);

    // space page: hall model, distance mode and the dry signal it drives (including the gap before the reverb)
    auto& dst = pages[kSpaceTab];
    addKnob (knobs, dst, ParamID::hallVolume, "SIZE");
    addKnob (knobs, dst, ParamID::hallAbsorption, "ABSORPTION");
    addKnob (knobs, dst, ParamID::distance, "DISTANCE");
    addKnob (knobs, dst, ParamID::distWidth, "SOURCE SIZE");
    addKnob (knobs, dst, ParamID::distMic, "MIC DISTANCE");
    addKnob (knobs, dst, ParamID::directHighs, "HIGHS", true);
    addKnob (knobs, dst, ParamID::directAir, "AIR");
    addKnob (knobs, dst, ParamID::directWidth, "WIDTH");
    addKnob (knobs, dst, ParamID::directDelay, "DELAY");
    addKnob (knobs, dst, ParamID::directGap, "REVERB GAP");
    hallOn = std::make_unique<ParamToggle> (state, ParamID::hallOn, "ON");
    distOn = std::make_unique<ParamToggle> (state, ParamID::distOn, "ON");
    for (Component* c : std::initializer_list<Component*> { hallOn.get(), distOn.get() })
        dst.addAndMakeVisible (c);
    hallOn->setTooltip (tooltipFor (ParamID::hallOn));
    distOn->setTooltip (tooltipFor (ParamID::distOn));
    distanceView.setTooltip ("Top view of the hall: source, listener, first reflection and the critical distance "
                             "(where direct sound and reverb are equally loud).");
    dst.addAndMakeVisible (distanceView);
    hallDecayLabel.setFont (uiFont (18.0f));
    hallDecayLabel.setJustificationType (Justification::centred);
    hallDecayLabel.setColour (Label::textColourId, Colours::lcdOn);
    hallDecayLabel.setTooltip ("The decay this hall gives. While HALL is on, DECAY follows it.");
    dst.addAndMakeVisible (hallDecayLabel);

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
    k->onTabKey = [this] (Knob& kn, bool backwards) { editNextKnob (kn, backwards); };
    k->onExtendMenu = [this] (Knob& kn, PopupMenu& menu) {
        const auto group = groupOf (kn.getParamID());
        auto& sh = processor.getShared();
        menu.addSeparator();
        menu.addItem ("Keep " + groupName (group).toLowerCase() + " when changing presets", true, sh.isKept (group),
                      [&sh, group] { sh.setKeptGroups (sh.getKeptGroups() ^ groupBit (group)); });
    };
    k->setTooltip (tooltipFor (id));
    parent.addAndMakeVisible (*k);
    list.push_back (std::move (k));
    return *list.back();
}

void MainPanel::editNextKnob (Knob& from, bool backwards)
{
    // the knobs next to it (same row of controls), left to right, top to bottom
    std::vector<Knob*> row;
    for (auto& k : knobs)
        if (k->getParentComponent() == from.getParentComponent() && k->isShowing() && k->isEnabled())
            row.push_back (k.get());
    std::sort (row.begin(), row.end(), [] (Knob* a, Knob* b) {
        const auto pa = a->getBounds().getCentre(), pb = b->getBounds().getCentre();
        return std::abs (pa.y - pb.y) > 40 ? pa.y < pb.y : pa.x < pb.x;
    });
    const auto it = std::find (row.begin(), row.end(), &from);
    if (it == row.end() || row.size() < 2) return;
    const int n = (int) row.size(), start = (int) (it - row.begin());
    for (int step = 1; step < n; ++step)
    {
        auto* next = row[(size_t) (((start + (backwards ? -step : step)) % n + n) % n)];
        next->showValueEditor();
        if (next->isEditingValue())
        {
            touch (*next);
            return;
        }
    }
}

void MainPanel::updateHistoryButtons()
{
    auto& h = processor.getHistory();
    const bool canUndo = h.canUndo(), canRedo = h.canRedo();
    undoButton.setEnabled (canUndo);
    redoButton.setEnabled (canRedo);
    undoButton.setTooltip (canUndo ? "Undo " + h.getUndoName() + " (Cmd/Ctrl+Z)" : String ("Nothing to undo"));
    redoButton.setTooltip (canRedo ? "Redo " + h.getRedoName() + " (Shift+Cmd+Z / Ctrl+Y)" : String ("Nothing to redo"));
    const int slot = processor.getABSlot();
    slotA.setToggleState (slot == 0, dontSendNotification);
    slotB.setToggleState (slot == 1, dontSendNotification);
    const auto copyText = slot == 0 ? "A > B" : "B > A";
    if (copySlot.getButtonText() != copyText || copySlot.getTooltip().isEmpty())
    {
        copySlot.setButtonText (copyText);
        copySlot.setTooltip (slot == 0 ? "Copies the current settings to B" : "Copies the current settings to A");
    }
}

bool MainPanel::keyPressed (const KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool z = key.getKeyCode() == 'Z' || key.getKeyCode() == 'z';
    const bool y = key.getKeyCode() == 'Y' || key.getKeyCode() == 'y';
    if (mods.isCommandDown() && z)
    {
        if (mods.isShiftDown()) processor.getHistory().redo();
        else processor.getHistory().undo();
        updateHistoryButtons();
        return true;
    }
    if (mods.isCommandDown() && y)
    {
        processor.getHistory().redo();
        updateHistoryButtons();
        return true;
    }
    return false; // everything else goes to the host (space bar ...)
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
        b->setTooltip (categoryTooltip (c));
        b->onClick = [this, c, ptr = b.get()] { showCategoryMenu (c, ptr); };
        addAndMakeVisible (*b);
        categoryButtons.push_back (std::move (b));
    }
    resized();
}

void MainPanel::showCategoryMenu (const String& category, Component* target)
{
    if (! processor.getShared().hasLibrary())
    {
        showWelcome (true);
        return;
    }
    PopupMenu menu;
    const auto current = processor.getPresetId();
    processor.getShared().withLibrary ([&] (const IRLibrary& lib) {
        const auto& presets = lib.getPresets();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const auto& p = presets[i];
            if (p.category == category)
                menu.addItem ((int) i + 1, p.displayNumber() + "  " + p.name, true, p.id == current);
        }
    });
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (target).withMinimumWidth (220),
                        [this, safe = SafePointer<MainPanel> (this)] (int result) {
                            if (safe == nullptr) return;
                            IRPreset p;
                            if (result > 0 && processor.getShared().getPresetAt (result - 1, p))
                                processor.selectPreset (p.id, true);
                        });
}

void MainPanel::showBrowser (bool show, const String& category)
{
    if (show && ! processor.getShared().hasLibrary() && processor.getShared().getUserPresets().getUserPresets().empty())
    {
        showWelcome (true);
        return;
    }
    browser.setCategory (category);
    if (show == browser.isVisible()) return;
    browser.setVisible (show);
    browseButton.setToggleState (show, dontSendNotification);
    if (show)
    {
        browser.toFront (false);
        browser.opened();
    }
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
    PopupMenu keep;
    const auto kept = shared.getKeptGroups();
    keep.addItem (20, "All settings (only the impulse response changes)", true, kept == kAllGroups);
    keep.addSeparator();
    for (int g = 0; g < kNumParamGroups; ++g)
        keep.addItem (30 + g, groupName ((ParamGroup) g), true, (kept & groupBit ((ParamGroup) g)) != 0);
    menu.addSubMenu ("Keep when changing presets", keep, true, nullptr, kept != 0);
    PopupMenu sizes;
    const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i)
        sizes.addItem (10 + i, String (roundToInt (scales[i] * 100.0f)) + " %", true,
                       std::abs (shared.getUIScale() - scales[i]) < 0.01f);
    menu.addSubMenu ("Interface size", sizes);
    menu.addSeparator();
    menu.addSectionHeader ("CPU (THIS INSTANCE)");
    menu.addItem (7, "Low CPU (adds " + String (Concert7Processor::kLowCpuBlock) + " samples latency)", true, processor.isLowCpu());
    menu.addItem (8, "Half rate convolution at 88.2 kHz and above", true, processor.isUnderSampling());
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
                            else if (r == 20) sh.setKeptGroups (sh.getKeptGroups() == kAllGroups ? 0 : kAllGroups);
                            else if (r >= 30 && r < 30 + kNumParamGroups)
                                sh.setKeptGroups (sh.getKeptGroups() ^ groupBit ((ParamGroup) (r - 30)));
                            else if (r >= 10 && r < 15)
                            {
                                const float chosen[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
                                sh.setUIScale (chosen[r - 10]);
                                if (onScaleChange) onScaleChange (chosen[r - 10]);
                            }
                            else if (r == 5) processor.selectPreset (processor.getPresetId(), true);
                            else if (r == 7) processor.setLowCpu (! processor.isLowCpu());
                            else if (r == 8) processor.setUnderSampling (! processor.isUnderSampling());
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
        browser.refresh(); // the library may have changed
    }
    if (show) showBrowser (false);
}

void MainPanel::showTab (int index)
{
    currentTab = jlimit (0, kNumTabs - 1, index);
    for (int i = 0; i < kNumTabs; ++i)
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

    updateHistoryButtons();

    // padlocks on the knobs of kept groups (the setting is shared by all instances)
    const auto kept = processor.getShared().getKeptGroups();
    if (kept != shownKept)
    {
        shownKept = kept;
        for (auto& k : knobs)
            k->setKept ((kept & groupBit (groupOf (k->getParamID()))) != 0);
    }
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

    // double-click on DECAY returns to the native decay of the current preset (all other knobs: their default,
    // DISTANCE: the start, where the sound is unchanged)
    if (status.nativeRT > 0.0f && status.nativeRT != lastNativeRT)
    {
        lastNativeRT = status.nativeRT;
        // ... and on VOLUME / ABSORPTION to the hall that gives that decay
        auto* p = state.getParameter (ParamID::decay);
        const float native = p->convertFrom0to1 (p->convertTo0to1 (jlimit (0.2f, 30.0f, status.nativeRT)));
        const auto absorption = state.getParameter (ParamID::hallAbsorption)->getNormalisableRange();
        const float volume = estimateHallVolume (native);
        for (auto& k : knobs)
        {
            if (k->getParamID() == ParamID::decay)
                k->getSlider().setDoubleClickReturnValue (true, native);
            else if (k->getParamID() == ParamID::hallVolume)
                k->getSlider().setDoubleClickReturnValue (true, volume);
            else if (k->getParamID() == ParamID::hallAbsorption)
                k->getSlider().setDoubleClickReturnValue (true, jlimit (absorption.start, absorption.end, eyringAbsorption (volume, native)));
        }
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

    // distance mode drives MIX and DIRECT (the greyed knobs show the values in effect);
    // its own controls only work while it is on
    const bool distance = processor.isDistanceOn();
    const bool hall = processor.isHallOn();
    const auto ds = processor.getDistanceState (false);
    for (auto& k : knobs)
    {
        const auto& pid = k->getParamID();
        const auto& d = ds.direct;
        auto driven = [&] (float value, const String& text) {
            k->setEnabled (! distance);
            if (distance) k->setDisplayOverride (value, text);
            else k->clearDisplayOverride();
        };
        if (pid == ParamID::mix)               driven (d.mix * 100.0f, String (roundToInt (d.mix * 100.0f)) + " %");
        else if (pid == ParamID::directHighs)  driven (d.highsDb, formatDb (d.highsDb));
        else if (pid == ParamID::directAir)    driven (d.airDb, d.airDb < 0.05f ? String ("Off") : "-" + String (d.airDb, 1) + " dB");
        else if (pid == ParamID::directWidth)  driven (d.width * 100.0f, String (roundToInt (d.width * 100.0f)) + " %");
        else if (pid == ParamID::directDelay)  driven (d.delayMs, formatMs (d.delayMs));
        else if (pid == ParamID::directGap)    driven (d.gapMs, formatMs (d.gapMs));
        if (pid == ParamID::decay)
        {
            // the hall model drives the decay
            k->setEnabled (! hall);
            const float decay = processor.getEffectiveDecay();
            if (hall) k->setDisplayOverride (decay, formatSeconds (decay));
            else k->clearDisplayOverride();
        }
        if (pid == ParamID::hallVolume || pid == ParamID::hallAbsorption)
        {
            // while off: the hall that matches the current decay
            k->setEnabled (hall);
            const float v = pid == ParamID::hallVolume ? processor.getHallVolume() : processor.getHallAbsorption();
            if (hall) k->clearDisplayOverride();
            else k->setDisplayOverride (v, pid == ParamID::hallVolume ? formatVolume (v) : formatAbsorption (v));
        }
        else if (pid == ParamID::distance || pid == ParamID::distWidth || pid == ParamID::distMic)
            k->setEnabled (distance);
        if (pid == ParamID::distance)
        {
            // double-click returns to the unchanged sound; while off, the knob shows where the mix sits
            k->getSlider().setDoubleClickReturnValue (true, ds.model.referenceDistance);
            if (distance)
                k->clearDisplayOverride();
            else
            {
                const auto range = state.getParameter (ParamID::distance)->getNormalisableRange();
                const float r0 = ds.model.referenceDistance;
                k->setDisplayOverride (r0, r0 >= range.end - 1.0e-3f ? "> " + formatMetres (range.end)
                                         : r0 <= range.start + 1.0e-3f ? "< " + formatMetres (range.start)
                                         : formatMetres (r0));
            }
        }
    }
    if (currentTab == kSpaceTab) distanceView.repaint();
    if (distance != dryAuto || hall != hallAuto)
    {
        dryAuto = distance;
        hallAuto = hall;
        repaint();   // the status lights of DRY SIGNAL and DECAY
    }

    // the decay the hall gives (or, while the hall model is off, the decay the hall matches)
    const auto decayText = formatSeconds (processor.getEffectiveDecay());
    if (hallDecayLabel.getText() != decayText)
        hallDecayLabel.setText (decayText, dontSendNotification);
    hallDecayLabel.setEnabled (hall);
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
    drawLogo (g, 40.0f, 64.0f);
    g.setColour (Colours::textDim);
    g.setFont (labelFont (10.0f));
    g.drawText ("TRUE STEREO REVERB", 42, 74, 220, 14, Justification::left);
    g.setColour (Colours::textFaint);
    g.drawText ("OPEN SOURCE", 42, 90, 220, 14, Justification::left);

    // power LED
    const bool ok = processor.getShared().hasLibrary();
    g.setColour ((ok ? Colours::ledOn : Colours::warning).withAlpha (0.3f));
    g.fillEllipse (151.0f, 91.0f, 12.0f, 12.0f);
    g.setColour (ok ? Colours::ledOn : Colours::warning);
    g.fillEllipse (154.0f, 94.0f, 6.0f, 6.0f);
    g.setColour (Colours::textFaint);
    g.setFont (labelFont (9.0f));
    g.drawText (ok ? "READY" : "NO IRS", 168, 91, 80, 12, Justification::left);

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
        frame (pages[0], { 724, 8, 220, 180 }, "TIMING / ROUTING");
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
    else if (currentTab == kSpaceTab)
    {
        using namespace SpaceGrid;
        auto& dst = pages[kSpaceTab];
        frame (dst, { hallX, topY, hallW, topH }, "HALL");
        frame (dst, { hallX, bottomY, hallW, bottomH }, "DECAY");
        frame (dst, { distX, topY, distW, 180 }, "DISTANCE");
        frame (dst, { dryX, topY, dryW, 180 }, "DRY SIGNAL");
        const auto origin = dst.getPosition().toFloat();

        // in the title row of what a section sets: lit while the section sets it
        auto status = [&] (int x, int y, int w, const String& text, bool on) {
            const auto font = labelFont (9.0f);
            const auto r = Rectangle<float> ((float) (x + 10), (float) y + 6.0f, (float) (w - 20), 12.0f) + origin;
            const float textX = r.getRight() - (float) GlyphArrangement::getStringWidthInt (font, text);
            const auto led = Rectangle<float> (6.0f, 6.0f).withCentre ({ textX - 9.0f, r.getCentreY() });
            if (on)
            {
                g.setColour (Colours::ledOn.withAlpha (0.35f));
                g.fillEllipse (led.expanded (3.0f));
            }
            g.setColour (on ? Colours::ledOn : Colours::ledOff);
            g.fillEllipse (led);
            g.setColour (on ? Colours::text : Colours::textFaint);
            g.setFont (font);
            g.drawText (text, r, Justification::centredRight);
        };
        status (hallX, bottomY, hallW, "SET BY HALL", hallAuto);
        status (dryX, topY, dryW, "SET BY DISTANCE", dryAuto);

        // the decay, as a readout (not a control)
        const auto lcd = hallDecayLabel.getBounds().toFloat() + origin;
        g.setColour (Colours::lcdBack);
        g.fillRoundedRectangle (lcd, 5.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (lcd.reduced (0.5f), 5.0f, 1.0f);
    }
}

void MainPanel::resized()
{
    browser.setBounds (0, kFaceHeight, kWidth, kHeight - kFaceHeight);
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
    // undo / redo and A/B below the logo
    undoButton.setBounds (40, 120, 44, 26);
    redoButton.setBounds (88, 120, 44, 26);
    slotA.setBounds (142, 120, 34, 26);
    slotB.setBounds (180, 120, 34, 26);
    copySlot.setBounds (218, 120, 42, 26);

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
    for (int i = 0; i < kNumTabs; ++i)
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
    small (rev, ParamID::irOffset, { 846, 22, 86, 92 });
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
    modMode->setBounds (24, 30, 208, 24);
    small (ext, ParamID::modDepth, { 24, 62, 100, 116 });
    small (ext, ParamID::modRate, { 132, 62, 100, 116 });
    small (ext, ParamID::duckAmount, { 270, 34, 100, 130 });
    small (ext, ParamID::duckThreshold, { 372, 34, 100, 130 });
    small (ext, ParamID::duckRelease, { 474, 34, 100, 130 });
    small (ext, ParamID::smooth, { 789, 34, 100, 130 });
    duckSidechain->setBounds (600, 160 - 8, 110, 24);

    // space page (see SpaceGrid): HALL is size and absorption, below it the DECAY they give. DISTANCE is the
    // main knob, its two options (off at the start of the knob) beside it. The DRY SIGNAL it sets: tone on top,
    // timing below, the image in the middle
    {
        using namespace SpaceGrid;
        auto& dst = pages[kSpaceTab];
        auto cell = [&] (const char* id, int x, int y, int w, int h, float textHeight = 12.0f) {
            for (auto& k : knobs)
                if (k->getParamID() == id)
                {
                    k->captionHeight = textHeight;
                    k->valueHeight = textHeight;
                    k->setBounds (x, y, w, h);
                    k->resized();
                }
        };
        hallOn->setBounds (hallX + hallW - 8 - 60, topY + 1, 60, 18);
        cell (ParamID::hallVolume, hallColumn (0), row1, knobW, knobH);
        cell (ParamID::hallAbsorption, hallColumn (1), row1, knobW, knobH);
        hallDecayLabel.setBounds (hallX + 20, bottomY + 26, hallW - 40, bottomH - 36);

        distOn->setBounds (distX + distW - 8 - 60, topY + 1, 60, 18);
        const int bigH = bigW + 30;
        cell (ParamID::distance, distX + 10, (row1 + row2 + knobH - bigH) / 2, bigW, bigH, 15.0f);
        cell (ParamID::distWidth, distX + 10 + bigW + 8, row1, optionW, knobH);
        cell (ParamID::distMic, distX + 10 + bigW + 8, row2, optionW, knobH);

        cell (ParamID::directHighs, dieColumn (0), row1, dieW, knobH);
        cell (ParamID::directAir, dieColumn (2), row1, dieW, knobH);
        cell (ParamID::directWidth, dieColumn (1), rowMiddle, dieW, knobH);
        cell (ParamID::directDelay, dieColumn (0), row2, dieW, knobH);
        cell (ParamID::directGap, dieColumn (2), row2, dieW, knobH);
        distanceView.setBounds (viewX, 8, dst.getWidth() - viewX - 8, 180);
    }

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
