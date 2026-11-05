// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Views.h"
#include "../dsp/IRLibrary.h"

namespace concert7
{
using namespace juce;

//==============================================================================
// PresetDisplay
//==============================================================================
void PresetDisplay::setStatus (const Concert7Processor::Status& s, bool lib)
{
    status = s;
    hasLibrary = lib;
    repaint();
}

void PresetDisplay::setReadout (const String& caption, const String& value)
{
    if (caption == readCaption && value == readValue) return;
    readCaption = caption;
    readValue = value;
    repaint();
}

void PresetDisplay::setIR (std::shared_ptr<const BuiltIR> newIR)
{
    if (newIR == ir) return;
    ir = std::move (newIR);
    repaint();
}

void PresetDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // bezel
    g.setGradientFill (ColourGradient (Colour (0xff050607), r.getX(), r.getY(), Colour (0xff2a2e33), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 9.0f);
    auto glass = r.reduced (5.0f);
    g.setColour (Colours::lcdBack);
    g.fillRoundedRectangle (glass, 6.0f);
    g.setGradientFill (ColourGradient (Colours::accent.withAlpha (0.07f), glass.getCentreX(), glass.getCentreY(),
                                       juce::Colours::transparentBlack, glass.getX(), glass.getY(), true));
    g.fillRoundedRectangle (glass, 6.0f);

    auto inner = glass.reduced (16.0f, 12.0f);
    auto graph = inner.removeFromRight (inner.getWidth() * 0.3f);
    inner.removeFromRight (14.0f);

    const auto on = Colours::lcdOn;
    const auto off = Colours::lcdOff;
    const float small = std::max (1.4f, inner.getHeight() / 52.0f);

    String line1, line2, line3, info;
    if (! hasLibrary)
    {
        line1 = "SETUP REQUIRED";
        line2 = "NO IRS LOADED";
        line3 = "CLICK HERE OR SETUP";
    }
    else
    {
        line1 = IRLibrary::shortCategoryName (status.category) + " " + status.number;
        line2 = status.presetName.toUpperCase();
        if (status.message.isNotEmpty())
            line3 = status.message.toUpperCase();
        else if (status.loading && ! status.hasIR)
            line3 = "LOADING...";
        else
            line3 = readCaption.toUpperCase() + "  " + readValue.toUpperCase();
        if (status.fileRate > 0)
            info = String (status.fileRate / 1000) + "K " + (status.trueStereo ? "TS" : "ST");
    }

    DotMatrix::drawText (g, line1, inner.getX(), inner.getY(), small, on, off);
    if (info.isNotEmpty())
        DotMatrix::drawText (g, info, inner.getRight() - DotMatrix::textWidth (info, small), inner.getY(), small,
                             on.withAlpha (0.7f), off);
    if (status.loading && hasLibrary)
    {
        const float d = small * 2.2f;
        g.setColour (on.withAlpha ((float) (0.4 + 0.4 * std::sin (Time::getMillisecondCounter() * 0.012))));
        g.fillEllipse (inner.getRight() - DotMatrix::textWidth (info, small) - d * 2.5f, inner.getY() + small * 2.5f, d, d);
    }

    float big = std::min (inner.getHeight() / 17.0f, inner.getWidth() / std::max (1.0f, line2.length() * 6.0f - 1.0f));
    big = std::max (1.6f, big);
    const float y2 = inner.getCentreY() - big * 4.0f;
    DotMatrix::drawText (g, line2, inner.getX(), y2, big, on, off);

    auto line3Pitch = std::min (small, inner.getWidth() / std::max (1.0f, line3.length() * 6.0f));
    DotMatrix::drawText (g, line3, inner.getX(), inner.getBottom() - 7.0f * line3Pitch, line3Pitch,
                         status.message.isNotEmpty() ? Colours::warning : on, off);

    paintGraph (g, graph);

    // glass reflection
    g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (isMouseOver() ? 0.07f : 0.045f), glass.getX(), glass.getY(),
                                       juce::Colours::transparentWhite, glass.getX(), glass.getCentreY(), false));
    g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.5f), 6.0f);
}

void PresetDisplay::paintGraph (Graphics& g, Rectangle<float> area)
{
    g.setColour (Colours::lcdOff);
    g.drawRoundedRectangle (area, 3.0f, 1.0f);
    for (int i = 1; i < 4; ++i)
    {
        const float y = area.getY() + area.getHeight() * i / 4.0f;
        for (float x = area.getX() + 3.0f; x < area.getRight() - 2.0f; x += 4.0f)
            g.fillRect (x, y, 1.2f, 1.2f);
    }
    if (ir == nullptr || ir->envelopeDb.empty()) return;

    auto plot = area.reduced (3.0f, 4.0f);
    plot.removeFromBottom (12.0f);
    const float minDb = -70.0f, maxDb = 6.0f;
    auto makePath = [&] (const std::vector<float>& env) {
        Path p;
        const size_t n = env.size();
        for (size_t i = 0; i < n; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
            const float y = jmap (jlimit (minDb, maxDb, env[i]), minDb, maxDb, plot.getBottom(), plot.getY());
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (Colours::lcdOn.withAlpha (0.28f));
    g.strokePath (makePath (ir->nativeEnvelopeDb), PathStrokeType (1.0f));
    auto built = makePath (ir->envelopeDb);
    g.setColour (Colours::lcdOn.withAlpha (0.25f));
    g.strokePath (built, PathStrokeType (3.0f));
    g.setColour (Colours::lcdOn);
    g.strokePath (built, PathStrokeType (1.2f));

    const auto label = String (ir->seconds, ir->seconds < 10.0f ? 1 : 0) + "S";
    const float p = 1.15f;
    DotMatrix::drawText (g, label, area.getRight() - DotMatrix::textWidth (label, p) - 4.0f, area.getBottom() - 11.0f, p,
                         Colours::lcdOn.withAlpha (0.8f), juce::Colours::transparentBlack, false);
    DotMatrix::drawText (g, "0", area.getX() + 4.0f, area.getBottom() - 11.0f, p, Colours::lcdOn.withAlpha (0.8f),
                         juce::Colours::transparentBlack, false);
}

void PresetDisplay::mouseDown (const MouseEvent&)
{
    if (onClick) onClick();
}

//==============================================================================
// DecayView
//==============================================================================
void DecayView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::lcdBack);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto plot = r.reduced (36.0f, 14.0f).withTrimmedBottom (8.0f).withTrimmedTop (8.0f);
    const float fMin = 40.0f, fMax = 16000.0f;
    auto fx = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };

    float maxRT = 1.0f;
    if (ir != nullptr)
        for (size_t i = 0; i < ir->rtFreqs.size(); ++i)
            if (ir->rtFreqs[i] >= fMin && ir->rtFreqs[i] <= fMax)
                maxRT = std::max ({ maxRT, ir->rtNative[i], ir->rtTarget[i] });
    // round the axis up to a "nice" value
    float axis = 1.0f;
    for (float v : { 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 20.0f, 30.0f, 60.0f, 120.0f })
        if (v >= maxRT * 1.08f) { axis = v; break; }
    auto ry = [&] (float rt) { return plot.getBottom() - plot.getHeight() * jlimit (0.0f, 1.0f, rt / axis); };

    g.setFont (uiFont (10.0f));
    for (float f : { 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f })
    {
        g.setColour (Colours::lcdOff.brighter (0.3f));
        g.drawVerticalLine (roundToInt (fx (f)), plot.getY(), plot.getBottom());
        g.setColour (Colours::textFaint);
        g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)),
                    Rectangle<float> (fx (f) - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), Justification::centred);
    }
    for (int i = 0; i <= 4; ++i)
    {
        const float rt = axis * i / 4.0f;
        g.setColour (Colours::lcdOff.brighter (0.3f));
        g.drawHorizontalLine (roundToInt (ry (rt)), plot.getX(), plot.getRight());
        g.setColour (Colours::textFaint);
        g.drawText (String (rt, rt < 10.0f && rt != (int) rt ? 1 : 0) + "s",
                    Rectangle<float> (r.getX() + 2.0f, ry (rt) - 6.0f, 32.0f, 12.0f), Justification::centredRight);
    }

    // shelf corner frequencies
    const float lowF = state.getRawParameterValue (ParamID::lowFreq)->load();
    const float highF = state.getRawParameterValue (ParamID::highFreq)->load();
    for (auto [f, name] : { std::pair<float, const char*> { lowF, "LOW" }, { highF, "HIGH" } })
    {
        if (f < fMin || f > fMax) continue;
        Path dash;
        dash.startNewSubPath (fx (f), plot.getY());
        dash.lineTo (fx (f), plot.getBottom());
        Path dashed;
        const float pattern[] = { 3.0f, 3.0f };
        PathStrokeType (1.0f).createDashedStroke (dashed, dash, pattern, 2);
        g.setColour (Colours::accent.withAlpha (0.45f));
        g.fillPath (dashed);
        g.setFont (labelFont (9.0f));
        g.drawText (name, Rectangle<float> (fx (f) + 3.0f, plot.getY(), 40.0f, 11.0f), Justification::left);
    }

    g.setColour (Colours::textDim);
    g.setFont (labelFont (9.5f));
    g.drawText ("REVERB TIME / FREQUENCY", r.reduced (10.0f, 5.0f).removeFromTop (12.0f), Justification::topRight);

    if (ir == nullptr || ir->rtFreqs.empty()) return;

    auto curve = [&] (const std::vector<float>& rt) {
        Path p;
        bool started = false;
        for (size_t i = 0; i < ir->rtFreqs.size(); ++i)
        {
            const float f = ir->rtFreqs[i];
            if (f < fMin * 0.99f || f > fMax * 1.01f) continue;
            const Point<float> pt (fx (f), ry (rt[i]));
            if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
        }
        return p;
    };

    Path native = curve (ir->rtNative), dashed;
    const float pattern[] = { 4.0f, 3.0f };
    PathStrokeType (1.3f).createDashedStroke (dashed, native, pattern, 2);
    g.setColour (Colours::textDim);
    g.fillPath (dashed);

    Path target = curve (ir->rtTarget);
    Path fill;
    {
        const auto bounds = target.getBounds();
        fill.startNewSubPath (bounds.getX(), plot.getBottom());
        for (size_t i = 0; i < ir->rtFreqs.size(); ++i)
        {
            const float f = ir->rtFreqs[i];
            if (f < fMin * 0.99f || f > fMax * 1.01f) continue;
            fill.lineTo (fx (f), ry (ir->rtTarget[i]));
        }
        fill.lineTo (bounds.getRight(), plot.getBottom());
        fill.closeSubPath();
    }
    g.setGradientFill (ColourGradient (Colours::accent.withAlpha (0.25f), 0.0f, plot.getY(), Colours::accent.withAlpha (0.02f), 0.0f,
                                       plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (Colours::accent);
    g.strokePath (target, PathStrokeType (2.0f));
}

//==============================================================================
// DistanceView
//==============================================================================
void DistanceView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::lcdBack);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    // while the mode is off, everything shows where the current mix places the source
    const bool on = processor.isDistanceOn();
    const auto ds = processor.getDistanceState();
    const auto& model = ds.model;
    const auto& d = ds.result;
    const bool hallOn = processor.isHallOn();
    const float mix = processor.getAPVTS().getRawParameterValue (ParamID::mix)->load();
    // source parts follow the distance mode, the hall follows the hall model
    const float dim = on ? 1.0f : 0.5f, hallDim = on || hallOn ? 1.0f : 0.5f;
    auto col = [dim] (Colour c, float alpha = 1.0f) { return c.withMultipliedAlpha (alpha * dim); };
    auto colH = [hallDim] (Colour c, float alpha = 1.0f) { return c.withMultipliedAlpha (alpha * hallDim); };

    auto inner = r.reduced (10.0f, 8.0f);

    // ---- values
    auto values = inner.removeFromRight (124.0f);
    values.removeFromTop (2.0f);
    auto row = [&] (const String& name, const String& value, bool hallValue = false) {
        auto line = values.removeFromTop (27.0f);
        const float rowDim = hallValue ? hallDim : dim;
        g.setColour (Colours::textFaint.withMultipliedAlpha (1.4f * rowDim));
        g.setFont (labelFont (8.5f));
        g.drawText (name, line.removeFromTop (12.0f), Justification::left);
        g.setColour (Colours::lcdOn.withMultipliedAlpha (rowDim));
        g.setFont (uiFont (12.5f));
        g.drawText (value, line, Justification::left);
    };
    const String drr = ds.drrDb >= 99.0f ? String ("direct only") : ds.drrDb <= -99.0f ? String ("reverb only")
                     : (ds.drrDb > 0.05f ? "+" : "") + String (ds.drrDb, 1) + " dB";
    Biquad shelf;
    shelf.highShelf (48000.0, kPresenceShelfHz, 0.6, d.presenceDb);
    const float highsDb = (float) (Decibels::gainToDecibels (shelf.magnitude (8000.0, 48000.0))
                                   - airAbsorptionDbPerMetre (8000.0, kAirHumidity) * d.airMetres);
    row ("DIRECT / REVERB", drr);
    row ("CRITICAL DISTANCE", formatMetres (ds.criticalDistance));
    row ("FIRST REFLECTION", String (ds.firstReflectionMs, 1) + " ms");
    row ("DIRECT AT 8 KHZ", std::abs (highsDb) < 0.05f ? String ("0.0 dB") : (highsDb > 0.0f ? "+" : "") + String (highsDb, 1) + " dB");
    row ("HALL VOLUME", formatVolume (d.volume), true);
    row ("ABSORPTION", describeAbsorption (processor.getHallAbsorption()), true);
    inner.removeFromRight (8.0f);

    // ---- top view of a shoebox hall (length 2 W, width W), stage on the left, in metres.
    // The full width always fits; the back of the hall is cut off only while nothing happens there.
    const auto plot = inner;
    const float W = hallWidthForVolume (d.volume), L = 2.0f * W;
    const float stageDepth = std::min (0.18f * L, 12.0f);
    const float sourceX = 0.55f * stageDepth;
    float scale = plot.getHeight() / W;
    const float reach = sourceX + 1.15f * std::max ({ model.distance, model.referenceDistance, ds.criticalDistance });
    if (std::max (L, reach) * scale > plot.getWidth() && reach * scale > plot.getWidth())
        scale = std::min (plot.getWidth() / L, plot.getHeight() / W);
    Point<float> origin (plot.getX(), plot.getCentreY());   // stage wall on the centre line
    if (L * scale < plot.getWidth())
        origin.x = plot.getCentreX() - 0.5f * L * scale;
    auto at = [&] (float x, float y) { return Point<float> (origin.x + x * scale, origin.y + y * scale); };
    const auto hall = Rectangle<float> (at (0.0f, -0.5f * W), at (L, 0.5f * W));

    Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    // floor, seating in two blocks with a centre aisle, balcony fronts (the side reflectors of the model)
    g.setColour (colH (Colours::lcdOff, 0.55f));
    g.fillRect (hall);
    const float rowStep = scale >= 4.0f ? 1.0f : 2.0f;
    g.setColour (colH (Colours::textFaint, 0.45f));
    for (float x = stageDepth + 2.5f; x < L - 1.5f; x += rowStep)
        for (float side : { -1.0f, 1.0f })
            g.drawLine (Line<float> (at (x, side * 1.0f), at (x, side * 0.46f * W)), 1.0f);
    const float pattern[] = { 4.0f, 3.0f };
    for (float side : { -1.0f, 1.0f })
    {
        const float y = side * kNearestReflectorFraction * W;
        g.setColour (colH (Colours::textDim, 0.7f));
        g.drawDashedLine (Line<float> (at (stageDepth, y), at (L, y)), pattern, 2, 1.0f);
    }

    // stage with a curved front
    Path stage;
    stage.startNewSubPath (at (0.0f, -0.42f * W));
    stage.lineTo (at (stageDepth, -0.42f * W));
    stage.quadraticTo (at (stageDepth * 1.25f, 0.0f), at (stageDepth, 0.42f * W));
    stage.lineTo (at (0.0f, 0.42f * W));
    stage.closeSubPath();
    g.setColour (colH (Colours::accentDim, 0.55f));
    g.fillPath (stage);
    g.setColour (colH (Colours::accentDim));
    g.strokePath (stage, PathStrokeType (1.0f));

    g.setColour (colH (Colours::textDim));
    g.drawRect (hall, 2.0f);

    const auto src = at (sourceX, 0.0f);
    const float micX = std::min (sourceX + model.distance, L - 1.0f);
    const bool beyond = sourceX + model.distance > L - 1.0f;
    const auto mic = at (micX, 0.0f);

    // critical distance: inside the circle the direct sound is louder than the reverb
    const float rc = ds.criticalDistance * scale;
    Path circle;
    circle.addEllipse (src.x - rc, src.y - rc, 2.0f * rc, 2.0f * rc);
    Path dashedCircle;
    PathStrokeType (1.0f).createDashedStroke (dashedCircle, circle, pattern, 2);
    g.setColour (col (Colours::accent, 0.08f));
    g.fillPath (circle);
    g.setColour (col (Colours::accent, 0.7f));
    g.fillPath (dashedCircle);
    g.setFont (labelFont (8.0f));
    auto rcLabel = Rectangle<float> (64.0f, 10.0f).withCentre ({ src.x, src.y - rc - 7.0f });
    rcLabel.setX (std::max (rcLabel.getX(), plot.getX() + 2.0f));
    if (plot.contains (rcLabel.getTopLeft()))
        g.drawText ("RC " + formatMetres (ds.criticalDistance), rcLabel, Justification::centred);

    // first reflection via the nearest balcony front
    Path reflection;
    reflection.startNewSubPath (src);
    reflection.lineTo (at (0.5f * (sourceX + micX), -kNearestReflectorFraction * W));
    reflection.lineTo (mic);
    Path dashedReflection;
    const float dots[] = { 2.0f, 3.0f };
    PathStrokeType (1.0f).createDashedStroke (dashedReflection, reflection, dots, 2);
    g.setColour (col (Colours::lcdOn, 0.6f));
    g.fillPath (dashedReflection);

    // direct sound with its length
    g.setColour (col (Colours::lcdOn));
    g.drawLine (Line<float> (src, mic), 1.5f);
    g.setFont (uiFont (11.0f));
    const String length = formatMetres (model.distance) + (beyond ? " >" : "");
    if (mic.x - src.x > 70.0f)
        g.drawText (length, Rectangle<float> (0.5f * (src.x + mic.x) - 40.0f, src.y - 17.0f, 80.0f, 13.0f), Justification::centred);
    else
        g.drawText (length, Rectangle<float> (mic.x + 20.0f, src.y - 6.5f, 60.0f, 13.0f), Justification::left);

    // where the source sounds like the unchanged preset
    if (on)
    {
        const auto ref = at (std::min (sourceX + model.referenceDistance, L - 1.0f), 0.0f);
        Path tri;
        tri.addTriangle (ref.x, ref.y + 4.0f, ref.x - 4.0f, ref.y + 10.0f, ref.x + 4.0f, ref.y + 10.0f);
        g.setColour (col (Colours::textDim));
        g.fillPath (tri);
        g.setFont (labelFont (7.5f));
        g.drawText ("START", Rectangle<float> (ref.x - 20.0f, ref.y + 11.0f, 40.0f, 10.0f), Justification::centred);
    }

    // loudspeaker on the stage, facing the hall
    {
        const float s = 6.0f;
        Path box, cone;
        box.addRoundedRectangle (src.x - 1.6f * s, src.y - s, 1.2f * s, 2.0f * s, 1.5f);
        cone.startNewSubPath (src.x - 0.4f * s, src.y - 0.55f * s);
        cone.lineTo (src.x + 0.9f * s, src.y - 1.3f * s);
        cone.lineTo (src.x + 0.9f * s, src.y + 1.3f * s);
        cone.lineTo (src.x - 0.4f * s, src.y + 0.55f * s);
        cone.closeSubPath();
        g.setColour (col (Colours::lcdBack));
        g.fillPath (box);
        g.fillPath (cone);
        g.setColour (col (Colours::lcdOn));
        g.strokePath (box, PathStrokeType (1.3f));
        g.strokePath (cone, PathStrokeType (1.3f));
    }

    // microphone pointing at the stage: capsule with grille, body behind it
    {
        const float s = 5.0f;
        const auto body = Rectangle<float> (mic.x + 0.6f * s, mic.y - 0.55f * s, 2.4f * s, 1.1f * s);
        g.setColour (col (Colours::lcdBack));
        g.fillRoundedRectangle (body, 1.5f);
        g.fillEllipse (mic.x - s, mic.y - s, 2.0f * s, 2.0f * s);
        g.setColour (col (Colours::lcdOn));
        g.drawRoundedRectangle (body, 1.5f, 1.3f);
        g.drawEllipse (mic.x - s, mic.y - s, 2.0f * s, 2.0f * s, 1.3f);
        for (float k : { -0.45f, 0.0f, 0.45f })
            g.drawLine (mic.x + k * s, mic.y - 0.8f * s, mic.x + k * s, mic.y + 0.8f * s, 0.8f);
    }

    // status line
    g.setFont (labelFont (8.5f));
    auto status = plot.reduced (4.0f, 3.0f).removeFromBottom (11.0f);
    g.setColour (Colours::lcdBack.withAlpha (0.8f));
    g.fillRect (status.withWidth (std::min (status.getWidth(), 230.0f)).expanded (3.0f, 1.0f));
    g.setColour (on ? Colours::textDim : Colours::text.withAlpha (0.8f));
    String text;
    if (on)
        text = "START " + formatMetres (model.referenceDistance) + "  >  " + formatMetres (model.distance);
    else if (mix >= 99.9f)
        text = "OFF - MIX 100 %: REVERB ONLY";
    else if (mix <= 0.1f)
        text = "OFF - MIX 0 %: DIRECT ONLY";
    else
        text = "OFF - YOUR MIX SITS AT " + formatMetres (model.distance);
    g.drawText (text, status, Justification::left);
}

//==============================================================================
// EQView
//==============================================================================
const EQView::Node EQView::nodes[5] = {
    { ParamID::eqHpFreq, nullptr, nullptr, ParamID::eqHpOn, "HP" },
    { ParamID::eqLowFreq, ParamID::eqLowGain, ParamID::eqLowQ, nullptr, "LO" },
    { ParamID::eqMidFreq, ParamID::eqMidGain, ParamID::eqMidQ, nullptr, "MID" },
    { ParamID::eqHighFreq, ParamID::eqHighGain, ParamID::eqHighQ, nullptr, "HI" },
    { ParamID::eqLpFreq, nullptr, nullptr, ParamID::eqLpOn, "LP" },
};

EQView::EQView (AudioProcessorValueTreeState& s, Concert7Processor& p) : state (s), processor (p)
{
    startTimerHz (30);
}

EQView::~EQView() { stopTimer(); }

void EQView::timerCallback()
{
    auto s = read();
    if (s != last) { last = s; repaint(); }
}

EQSettings EQView::read() const
{
    auto v = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
    EQSettings s;
    s.on = v (ParamID::eqOn) > 0.5f;
    s.hpOn = v (ParamID::eqHpOn) > 0.5f;   s.hpFreq = v (ParamID::eqHpFreq);
    s.lowType = roundToInt (v (ParamID::eqLowType));
    s.lowFreq = v (ParamID::eqLowFreq);    s.lowGain = v (ParamID::eqLowGain);   s.lowQ = v (ParamID::eqLowQ);
    s.midFreq = v (ParamID::eqMidFreq);    s.midGain = v (ParamID::eqMidGain);   s.midQ = v (ParamID::eqMidQ);
    s.highType = roundToInt (v (ParamID::eqHighType));
    s.highFreq = v (ParamID::eqHighFreq);  s.highGain = v (ParamID::eqHighGain); s.highQ = v (ParamID::eqHighQ);
    s.lpOn = v (ParamID::eqLpOn) > 0.5f;   s.lpFreq = v (ParamID::eqLpFreq);
    return s;
}

Rectangle<float> EQView::plotArea() const { return getLocalBounds().toFloat().reduced (30.0f, 12.0f).withTrimmedBottom (6.0f); }
float EQView::freqToX (float f) const { auto p = plotArea(); return p.getX() + p.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
float EQView::xToFreq (float x) const { auto p = plotArea(); return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
float EQView::gainToY (float db) const { auto p = plotArea(); return jmap (db, -18.0f, 18.0f, p.getBottom(), p.getY()); }
float EQView::yToGain (float y) const { auto p = plotArea(); return jlimit (-18.0f, 18.0f, jmap (y, p.getBottom(), p.getY(), -18.0f, 18.0f)); }

Point<float> EQView::nodePosition (int i, const EQSettings& s) const
{
    const float f = state.getRawParameterValue (nodes[i].freq)->load();
    const float g = nodes[i].gain != nullptr ? state.getRawParameterValue (nodes[i].gain)->load() : 0.0f;
    ignoreUnused (s);
    return { freqToX (f), gainToY (g) };
}

int EQView::nodeAt (Point<float> p) const
{
    const auto s = read();
    for (int i = 4; i >= 0; --i)
        if (nodePosition (i, s).getDistanceFrom (p) < 9.0f)
            return i;
    return -1;
}

void EQView::setParam (const char* id, float value)
{
    if (auto* p = state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void EQView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::lcdBack);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    const auto plot = plotArea();
    g.setFont (uiFont (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (Colours::lcdOff.brighter (0.3f));
        g.drawVerticalLine (roundToInt (freqToX (f)), plot.getY(), plot.getBottom());
        g.setColour (Colours::textFaint);
        g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)),
                    Rectangle<float> (freqToX (f) - 20.0f, plot.getBottom() + 1.0f, 40.0f, 11.0f), Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour (db == 0.0f ? Colours::lcdOff.brighter (0.8f) : Colours::lcdOff.brighter (0.3f));
        g.drawHorizontalLine (roundToInt (gainToY (db)), plot.getX(), plot.getRight());
        g.setColour (Colours::textFaint);
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (2.0f, gainToY (db) - 6.0f, 24.0f, 12.0f),
                    Justification::centredRight);
    }

    const auto s = read();
    const double sr = processor.getSampleRateForUI() > 0.0 ? processor.getSampleRateForUI() : 48000.0;
    Path curve;
    for (int i = 0; i <= 240; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * i / 240.0f;
        const float f = xToFreq (x);
        const float y = gainToY (jlimit (-20.0f, 20.0f, (float) PostEQ::responseDb (s, sr, f)));
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    Path fill (curve);
    fill.lineTo (plot.getRight(), gainToY (0.0f));
    fill.lineTo (plot.getX(), gainToY (0.0f));
    fill.closeSubPath();
    const auto col = s.on ? Colours::accent : Colours::textFaint;
    g.setColour (col.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (col);
    g.strokePath (curve, PathStrokeType (2.0f));

    for (int i = 0; i < 5; ++i)
    {
        const auto p = nodePosition (i, s);
        bool active = true;
        if (nodes[i].enable != nullptr) active = state.getRawParameterValue (nodes[i].enable)->load() > 0.5f;
        else active = std::abs (state.getRawParameterValue (nodes[i].gain)->load()) > 0.01f;
        const float rad = (i == hover || i == dragging) ? 7.0f : 5.5f;
        g.setColour (active && s.on ? Colours::accent : Colours::textFaint);
        g.fillEllipse (p.x - rad, p.y - rad, rad * 2, rad * 2);
        g.setColour (Colours::lcdBack);
        g.drawEllipse (p.x - rad, p.y - rad, rad * 2, rad * 2, 1.5f);
        g.setColour (Colours::textDim);
        g.setFont (labelFont (8.5f));
        g.drawText (nodes[i].label, Rectangle<float> (p.x - 15.0f, p.y - rad - 13.0f, 30.0f, 11.0f), Justification::centred);
    }
}

void EQView::mouseMove (const MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}

void EQView::mouseDown (const MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging < 0) return;
    const auto& n = nodes[dragging];
    if (n.enable != nullptr && state.getRawParameterValue (n.enable)->load() < 0.5f)
        setParam (n.enable, 1.0f);
    for (auto* id : { n.freq, n.gain })
        if (id != nullptr)
            if (auto* p = state.getParameter (id)) p->beginChangeGesture();
}

void EQView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const auto& n = nodes[dragging];
    if (auto* p = dynamic_cast<RangedAudioParameter*> (state.getParameter (n.freq)))
        setParam (n.freq, p->getNormalisableRange().snapToLegalValue (xToFreq (e.position.x)));
    if (n.gain != nullptr)
        setParam (n.gain, yToGain (e.position.y));
    repaint();
}

void EQView::mouseUp (const MouseEvent&)
{
    if (dragging < 0) return;
    const auto& n = nodes[dragging];
    for (auto* id : { n.freq, n.gain })
        if (id != nullptr)
            if (auto* p = state.getParameter (id)) p->endChangeGesture();
    dragging = -1;
}

void EQView::mouseDoubleClick (const MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h < 0) return;
    if (nodes[h].enable != nullptr)
        setParam (nodes[h].enable, state.getRawParameterValue (nodes[h].enable)->load() > 0.5f ? 0.0f : 1.0f);
    else
        setParam (nodes[h].gain, 0.0f);
}

void EQView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int h = nodeAt (e.position);
    if (h < 0 || nodes[h].q == nullptr) return;
    const float q = state.getRawParameterValue (nodes[h].q)->load();
    setParam (nodes[h].q, jlimit (0.3f, 6.0f, q * std::pow (1.25f, w.deltaY * 4.0f)));
}

//==============================================================================
// WelcomeOverlay
//==============================================================================
namespace
{
    bool germanUI()
    {
        // CONCERT7_LANG=en|de overrides the system language (used for testing)
        static const String forced = SystemStats::getEnvironmentVariable ("CONCERT7_LANG", {});
        static const bool german = forced.isNotEmpty() ? forced.startsWithIgnoreCase ("de")
                                 : (SystemStats::getUserLanguage().startsWithIgnoreCase ("de")
                                    || SystemStats::getDisplayLanguage().startsWithIgnoreCase ("de"));
        return german;
    }

    /** English / German text depending on the system language. */
    String tr (const char* en, const char* de) { return String::fromUTF8 (germanUI() ? de : en); }

    String shortPath (const File& f)
    {
        auto path = f.getFullPathName();
        const auto home = File::getSpecialLocation (File::userHomeDirectory).getFullPathName();
        if (path.startsWith (home)) path = "~" + path.substring (home.length());
        return path;
    }

    void drawCheck (Graphics& g, Rectangle<float> r)
    {
        Path p;
        p.startNewSubPath (r.getX() + r.getWidth() * 0.22f, r.getCentreY());
        p.lineTo (r.getX() + r.getWidth() * 0.43f, r.getY() + r.getHeight() * 0.7f);
        p.lineTo (r.getRight() - r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.3f);
        g.strokePath (p, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

WelcomeOverlay::WelcomeOverlay (Concert7Processor& p) : processor (p)
{
    pageButton.setButtonText (tr ("Open download page", u8"Download-Seite öffnen"));
    zipButton.setButtonText (tr ("Choose ZIP file...", u8"ZIP-Datei auswählen …"));
    folderButton.setButtonText (tr ("Already unpacked? Choose folder...", u8"Schon entpackt? Ordner wählen …"));
    pageButton.setTooltip ("Opens Samplicity's downloads page; the Bricasti M7 archive is listed at the bottom.");
    zipButton.setTooltip ("Pick the downloaded ZIP (about 1 GB). Concert7 checks it and installs the impulse responses.");
    folderButton.setTooltip ("Use a folder with unpacked impulse responses, your own included.");
    closeButton.setTooltip ("Closes the setup. It stays available under SETUP.");

    for (auto* b : { &pageButton, &zipButton, &folderButton, &closeButton })
    {
        b->getProperties().set ("plain", true);
        addAndMakeVisible (b);
    }
    pageButton.getProperties().set ("primary", true);
    closeButton.getProperties().set ("primary", true);
    folderButton.getProperties().set ("link", true);
    addChildComponent (progressBar);
    progressBar.setPercentageDisplay (false);

    pageButton.onClick = [this] {
        URL (IRLibrary::kDownloadPage).launchInDefaultBrowser();
        pageOpened = true;
        repaint();
    };
    zipButton.onClick = [this] { chooseZip(); };
    folderButton.onClick = [this] { chooseFolder(); };
    closeButton.onClick = [this] {
        processor.getShared().setWelcomeDone (true);
        if (onClose) onClose();
    };

    refresh();
    startTimerHz (15);
}

WelcomeOverlay::~WelcomeOverlay() { stopTimer(); }

void WelcomeOverlay::chooseZip()
{
    chooser = std::make_unique<FileChooser> (tr ("Choose the Samplicity Bricasti ZIP archive", u8"Samplicity-Bricasti-ZIP-Archiv auswählen"),
                                             File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"),
                                             "*.zip");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [safe = SafePointer<WelcomeOverlay> (this)] (const FileChooser& fc) {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->acceptFile (fc.getResult());
                          });
}

void WelcomeOverlay::chooseFolder()
{
    chooser = std::make_unique<FileChooser> (tr ("Choose the folder with the extracted impulse responses",
                                                 u8"Ordner mit den entpackten Impulsantworten wählen"),
                                             processor.getShared().getIRFolder().exists()
                                                 ? processor.getShared().getIRFolder()
                                                 : File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [safe = SafePointer<WelcomeOverlay> (this)] (const FileChooser& fc) {
                              if (safe != nullptr && fc.getResult().isDirectory())
                                  safe->acceptFile (fc.getResult());
                          });
}

bool WelcomeOverlay::isInterestedInFileDrag (const StringArray& files)
{
    if (processor.getShared().getInstaller().isBusy()) return false;
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".zip") || File (f).isDirectory())
            return true;
    return false;
}

void WelcomeOverlay::filesDropped (const StringArray& files, int, int)
{
    setDragHighlight (false);
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".zip") || File (f).isDirectory())
        {
            acceptFile (File (f));
            return;
        }
}

void WelcomeOverlay::acceptFile (const File& f)
{
    auto& shared = processor.getShared();
    localError.clear();
    if (f.isDirectory())
    {
        shared.setIRFolder (f);
        if (! shared.hasLibrary())
            localError = tr ("No impulse responses found in this folder.", "In diesem Ordner wurden keine Impulsantworten gefunden.");
    }
    else if (f.hasFileExtension ("zip"))
    {
        shared.getInstaller().start (f, SharedState::getDefaultInstallFolder());
    }
    refresh();
    repaint();
}

void WelcomeOverlay::timerCallback() { refresh(); }

WelcomeOverlay::ZoneState WelcomeOverlay::zoneState() const
{
    auto& shared = processor.getShared();
    const auto phase = shared.getInstaller().getPhase();
    if (phase == IRInstaller::Phase::Checking || phase == IRInstaller::Phase::Extracting) return ZoneState::Busy;
    if (localError.isNotEmpty() || phase == IRInstaller::Phase::Failed) return ZoneState::Failed;
    if (shared.hasLibrary()) return ZoneState::Done;
    return ZoneState::Idle;
}

void WelcomeOverlay::refresh()
{
    auto& shared = processor.getShared();
    auto& installer = shared.getInstaller();
    const auto state = zoneState();
    const bool busy = state == ZoneState::Busy;

    progressValue = installer.getPhase() == IRInstaller::Phase::Extracting ? installer.getProgress() : -1.0;
    progressBar.setVisible (busy);
    zipButton.setVisible (! busy);
    zipButton.setButtonText (state == ZoneState::Done ? tr ("Install another ZIP...", u8"Andere ZIP installieren …")
                                                      : tr ("Choose ZIP file...", u8"ZIP-Datei auswählen …"));
    zipButton.setEnabled (! busy);
    folderButton.setEnabled (! busy);
    closeButton.setEnabled (! busy);
    closeButton.setButtonText (shared.hasLibrary() ? tr ("Start", "Los geht's") : tr ("Later", u8"Später"));
    closeButton.getProperties().set ("primary", shared.hasLibrary());

    const int hash = (int) state * 1000 + (int) installer.getPhase() * 100 + roundToInt (installer.getProgress() * 99.0f)
                   + shared.getNumPresets() * 7 + (pageOpened ? 50000 : 0);
    if (hash != lastHash)
    {
        lastHash = hash;
        resized();
        repaint();
    }
}

Rectangle<int> WelcomeOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (std::min (720, getWidth() - 40), std::min (536, getHeight() - 24));
}

Rectangle<int> WelcomeOverlay::dropZone() const
{
    auto card = cardBounds();
    return { card.getX() + 76, card.getY() + 314, card.getWidth() - 76 - 32, 128 };
}

void WelcomeOverlay::resized()
{
    const auto card = cardBounds();
    pageButton.setBounds (card.getRight() - 32 - 190, card.getY() + 132, 190, 34);
    const auto zone = dropZone();
    zipButton.setBounds (zone.withSizeKeepingCentre (200, 30).withY (zone.getBottom() - 44));
    progressBar.setBounds (zone.reduced (40, 0).withHeight (10).withY (zone.getBottom() - 36));
    closeButton.setBounds (card.getRight() - 32 - 160, card.getBottom() - 56, 160, 36);
    folderButton.setBounds (card.getX() + 24, card.getBottom() - 56, 300, 36);
}

void WelcomeOverlay::paintStep (Graphics& g, Rectangle<float> row, int number, bool done, const String& title, const String& text)
{
    auto badge = Rectangle<float> (26.0f, 26.0f).withPosition (row.getX(), row.getY());
    g.setColour (done ? Colours::accent : Colours::panelLight);
    g.fillEllipse (badge);
    g.setColour (done ? Colours::accent : Colours::accent.withAlpha (0.7f));
    g.drawEllipse (badge.reduced (0.75f), 1.5f);
    if (done)
    {
        g.setColour (Colours::lcdBack);
        drawCheck (g, badge.reduced (5.0f));
    }
    else
    {
        g.setColour (Colours::accent);
        g.setFont (uiFont (14.0f, true));
        g.drawText (String (number), badge, Justification::centred);
    }

    auto textArea = row.withTrimmedLeft (44.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText (title, textArea.removeFromTop (22.0f), Justification::centredLeft);
    if (text.isNotEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (text, textArea.removeFromTop (40.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
    }
}

void WelcomeOverlay::paintDropZone (Graphics& g, Rectangle<float> zone)
{
    const auto state = zoneState();
    const bool highlight = dragOver && state != ZoneState::Busy;
    const auto accent = state == ZoneState::Failed ? Colours::warning : Colours::accent;

    g.setColour (highlight ? accent.withAlpha (0.12f) : Colours::lcdBack.withAlpha (0.6f));
    g.fillRoundedRectangle (zone, 8.0f);
    Path border, dashed;
    border.addRoundedRectangle (zone.reduced (1.0f), 8.0f);
    const float dashes[] = { 7.0f, 5.0f };
    PathStrokeType (highlight ? 2.0f : 1.4f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (highlight ? accent : (state == ZoneState::Idle ? Colours::outline.brighter (0.5f) : accent.withAlpha (0.8f)));
    g.fillPath (dashed);

    auto inner = zone.reduced (16.0f, 12.0f);
    auto iconArea = inner.removeFromTop (34.0f).withSizeKeepingCentre (34.0f, 34.0f);
    auto& installer = processor.getShared().getInstaller();

    String line1, line2;
    Colour line1Colour = Colours::text;
    if (state == ZoneState::Busy)
    {
        const bool extracting = installer.getPhase() == IRInstaller::Phase::Extracting;
        line1 = extracting ? tr ("Unpacking impulse responses...", u8"Impulsantworten werden entpackt …")
                           : tr ("Checking archive...", u8"Archiv wird geprüft …");
        line2 = extracting ? String (roundToInt (installer.getProgress() * 100.0f)) + " %" : String();
        g.setColour (Colours::accent);
        g.drawEllipse (iconArea.reduced (4.0f), 2.0f);
        const float a = (float) (Time::getMillisecondCounter() % 1200) / 1200.0f * MathConstants<float>::twoPi;
        Path arc;
        arc.addCentredArc (iconArea.getCentreX(), iconArea.getCentreY(), 13.0f, 13.0f, 0.0f, a, a + 1.6f, true);
        g.setColour (Colours::lcdOn);
        g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else if (state == ZoneState::Done)
    {
        int numPresets = 0; // Bricasti and user impulse responses
        processor.getShared().withLibrary ([&numPresets] (const IRLibrary& lib) { numPresets = (int) lib.getPresets().size(); });
        g.setColour (Colours::accent);
        g.fillEllipse (iconArea.reduced (2.0f));
        g.setColour (Colours::lcdBack);
        drawCheck (g, iconArea.reduced (8.0f));
        line1 = tr ("Installed", "Installiert") + ": " + String (numPresets) + " " + tr ("presets", "Presets");
        line2 = shortPath (processor.getShared().getIRFolder());
    }
    else
    {
        // tray with an arrow
        Path icon;
        const auto c = iconArea.getCentre();
        icon.startNewSubPath (c.x, c.y - 14.0f);
        icon.lineTo (c.x, c.y + 4.0f);
        icon.startNewSubPath (c.x - 7.0f, c.y - 3.0f);
        icon.lineTo (c.x, c.y + 4.0f);
        icon.lineTo (c.x + 7.0f, c.y - 3.0f);
        icon.startNewSubPath (c.x - 15.0f, c.y + 2.0f);
        icon.lineTo (c.x - 15.0f, c.y + 13.0f);
        icon.lineTo (c.x + 15.0f, c.y + 13.0f);
        icon.lineTo (c.x + 15.0f, c.y + 2.0f);
        g.setColour (highlight || state == ZoneState::Failed ? accent : Colours::textDim);
        g.strokePath (icon, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));

        if (state == ZoneState::Failed)
        {
            line1Colour = Colours::warning;
            const auto err = installer.getError();
            line1 = localError.isNotEmpty() ? localError
                  : err == IRInstaller::Error::NotBricasti ? tr ("This ZIP is not the Samplicity Bricasti library.",
                                                                 u8"Diese ZIP ist nicht die Samplicity-Bricasti-Bibliothek.")
                  : err == IRInstaller::Error::CannotOpen ? tr ("The ZIP file could not be opened (incomplete download?).",
                                                                u8"Die ZIP-Datei lässt sich nicht öffnen (Download unvollständig?).")
                  : err == IRInstaller::Error::Cancelled ? tr ("Installation cancelled.", "Installation abgebrochen.")
                  : tr ("Unpacking failed: ", "Entpacken fehlgeschlagen: ") + installer.getErrorDetail();
            line2 = tr ("Drop the ZIP again or choose it below.", u8"ZIP erneut hier ablegen oder unten auswählen.");
        }
        else
        {
            line1 = highlight ? tr ("Release to install", "Loslassen zum Installieren")
                              : tr (u8"Drag the ZIP file here (“Samplicity - Bricasti IRs version 2023-10.zip”)",
                                    u8"ZIP-Datei hierher ziehen („Samplicity - Bricasti IRs version 2023-10.zip“)");
            line2 = highlight ? String() : tr ("or", "oder");
        }
    }

    g.setColour (line1Colour);
    g.setFont (uiFont (14.0f, true));
    g.drawFittedText (line1, inner.removeFromTop (22.0f).toNearestInt(), Justification::centred, 1);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (12.5f));
    g.drawFittedText (line2, inner.removeFromTop (18.0f).toNearestInt(), Justification::centred, 1);
}

void WelcomeOverlay::paint (Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.8f));
    const auto card = cardBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (card.translated (0.0f, 6.0f).expanded (2.0f), 14.0f);
    g.setGradientFill (ColourGradient (Colours::panelLight, card.getX(), card.getY(), Colours::panel, card.getX(), card.getBottom(), false));
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (Colours::outline.brighter (0.25f));
    g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);

    auto r = card.reduced (32.0f, 26.0f);
    DotMatrix::drawText (g, "CONCERT7 SETUP", r.getX(), r.getY(), 2.2f, Colours::lcdOn, juce::Colours::transparentBlack);
    r.removeFromTop (34.0f);

    g.setColour (Colours::text);
    g.setFont (uiFont (14.0f));
    g.drawFittedText (tr ("Concert7 needs the free Bricasti M7 impulse response library by Samplicity. It is not included "
                          "with the plugin – install it in three steps:",
                          u8"Concert7 benötigt die kostenlose Bricasti-M7-Impulsantwort-Bibliothek von Samplicity. Sie ist "
                          u8"nicht im Plugin enthalten – so installierst du sie in drei Schritten:"),
                      r.removeFromTop (42.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
    r.removeFromTop (12.0f);

    const bool installed = processor.getShared().hasLibrary();
    auto step1 = r.removeFromTop (94.0f).withTrimmedRight (210.0f);
    paintStep (g, step1, 1, installed || pageOpened,
               tr ("Open Samplicity's download page", u8"Download-Seite von Samplicity öffnen"),
               tr ("Samplicity provides the library for free. If you use it, you can support them by buying "
                   "one of their reverb plugins.",
                   u8"Samplicity stellt die Bibliothek kostenlos bereit. Wenn du sie nutzt, kannst du Samplicity "
                   u8"mit dem Kauf eines ihrer Reverb-Plugins unterstützen."));
    g.setColour (Colours::accent.withAlpha (0.8f));
    g.setFont (uiFont (12.0f));
    g.drawText ("samplicity.com/downloads", step1.withTrimmedLeft (44.0f).withTrimmedBottom (14.0f).removeFromBottom (16.0f),
                Justification::centredLeft);

    auto step2 = r.removeFromTop (58.0f);
    paintStep (g, step2, 2, installed,
               tr ("Download the ZIP archive at the bottom of the page (about 1 GB)",
                   u8"ZIP-Archiv ganz unten auf der Seite herunterladen (ca. 1 GB)"),
               tr (u8"Listed as “Bricasti M7 impulse response archive”. No need to unpack it – Concert7 does that for you.",
                   u8"Eintrag „Bricasti M7 impulse response archive“. Entpacken ist nicht nötig – das übernimmt Concert7."));

    auto step3 = r.removeFromTop (30.0f);
    paintStep (g, step3, 3, installed, tr ("Drop the ZIP file into Concert7", u8"ZIP-Datei in Concert7 ablegen"), {});

    paintDropZone (g, dropZone().toFloat());

    // footer separator
    g.setColour (Colours::outline);
    g.fillRect (card.getX() + 24.0f, card.getBottom() - 72.0f, card.getWidth() - 48.0f, 1.0f);
}
} // namespace concert7
