// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Theme.h"

namespace concert7
{
using namespace juce;

Font uiFont (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain));
}

Font labelFont (float height)
{
    return Font (FontOptions (height, Font::bold)).withExtraKerningFactor (0.12f);
}

void drawScrew (Graphics& g, Point<float> c, float r)
{
    g.setGradientFill (ColourGradient (Colour (0xff6c727b), c.x - r, c.y - r, Colour (0xff24272c), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colour (0xff0d0f12));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);
    g.setColour (Colour (0xff15171a));
    Path slot;
    slot.addRectangle (-r * 0.75f, -r * 0.13f, r * 1.5f, r * 0.26f);
    g.fillPath (slot, AffineTransform::rotation (0.6f).translated (c));
}

//==============================================================================
Concert7LookAndFeel::Concert7LookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, Colours::background);
    setColour (PopupMenu::backgroundColourId, Colours::panel);
    setColour (PopupMenu::textColourId, Colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Colours::accentDim);
    setColour (PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (ComboBox::backgroundColourId, Colours::panelLight);
    setColour (ComboBox::textColourId, Colours::text);
    setColour (ComboBox::outlineColourId, Colours::outline);
    setColour (ComboBox::arrowColourId, Colours::accent);
    setColour (TextButton::buttonColourId, Colours::panelLight);
    setColour (TextButton::buttonOnColourId, Colours::accentDim);
    setColour (TextButton::textColourOffId, Colours::textDim);
    setColour (TextButton::textColourOnId, Colours::text);
    setColour (Slider::textBoxTextColourId, Colours::text);
    setColour (Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (Label::textColourId, Colours::text);
    setColour (TooltipWindow::backgroundColourId, Colours::panelLight);
    setColour (TooltipWindow::textColourId, Colours::text);
    setColour (TooltipWindow::outlineColourId, Colours::outline);
    setColour (BubbleComponent::backgroundColourId, Colours::panelLight);
    setColour (BubbleComponent::outlineColourId, Colours::outline);
    setColour (TextEditor::backgroundColourId, Colours::lcdBack);
    setColour (TextEditor::textColourId, Colours::lcdOn);
    setColour (TextEditor::highlightColourId, Colours::accentDim);
    setColour (TextEditor::outlineColourId, Colours::outline);
    setColour (TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (CaretComponent::caretColourId, Colours::lcdOn);
    setColour (HyperlinkButton::textColourId, Colours::accent);
    setColour (AlertWindow::backgroundColourId, Colours::panel);
    setColour (AlertWindow::textColourId, Colours::text);
    setColour (AlertWindow::outlineColourId, Colours::outline);
    setColour (ProgressBar::backgroundColourId, Colours::lcdBack);
    setColour (ProgressBar::foregroundColourId, Colours::accent);
}

void Concert7LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start,
                                            float end, Slider& slider)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = std::min (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = size * 0.5f;
    if (auto* shown = slider.getProperties().getVarPointer ("displayPos"))
        pos = (float) *shown;   // value driven by another mode (see Knob::setDisplayOverride)
    const float angle = start + pos * (end - start);
    const bool bipolar = slider.getProperties()["bipolar"];
    const bool enabled = slider.isEnabled();
    const float track = std::max (2.0f, r * 0.09f);
    const float arcR = r - track * 0.5f;

    // track
    Path bg;
    bg.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Colour (0xff0b0c0e));
    g.strokePath (bg, PathStrokeType (track + 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff2a2e34));
    g.strokePath (bg, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.01f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
        auto col = enabled ? Colours::accent : Colours::textFaint;
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (val, PathStrokeType (track + 4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (val, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // knob body
    const float kr = r * 0.74f;
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - kr, c.y - kr + kr * 0.08f, 2 * kr, 2 * kr);
    g.setGradientFill (ColourGradient (Colour (0xff59606a), c.x - kr * 0.6f, c.y - kr,
                                       Colour (0xff1c1f23), c.x + kr * 0.5f, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
    const float ir = kr * 0.84f;
    g.setGradientFill (ColourGradient (Colour (0xff2e3238), c.x, c.y - ir, Colour (0xff23262b), c.x, c.y + ir, false));
    g.fillEllipse (c.x - ir, c.y - ir, 2 * ir, 2 * ir);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (c.x - kr + 0.5f, c.y - kr + 0.5f, 2 * kr - 1.0f, 2 * kr - 1.0f, 1.0f);

    // pointer
    Path ptr;
    const float pw = std::max (2.0f, kr * 0.1f);
    ptr.addRoundedRectangle (-pw * 0.5f, -kr * 0.92f, pw, kr * 0.5f, pw * 0.5f);
    g.setColour (enabled ? juce::Colours::white.withAlpha (0.92f) : Colours::textFaint);
    g.fillPath (ptr, AffineTransform::rotation (angle).translated (c));
}

void Concert7LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const bool tab = b.getProperties()["tab"];
    const float corner = 4.0f;

    if (tab)
    {
        g.setColour (on ? Colours::panelLight : (over ? Colours::panel.brighter (0.05f) : Colours::panel));
        g.fillRoundedRectangle (r, corner);
        if (on)
        {
            g.setColour (Colours::accent);
            g.fillRoundedRectangle (r.removeFromBottom (2.5f).reduced (8.0f, 0.0f), 1.0f);
        }
        return;
    }

    if (b.getProperties()["folderTab"])
    {
        // a tab on the tray below it (drawn by the parent; the tab overlaps its edge): the chosen one has the tray's
        // colour and is open at the bottom, so it reads as part of the tray
        Path shape;
        shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 3.0f, corner, corner, true, true, false, false);
        if (on)
        {
            g.setColour (Colours::tray);
            g.fillPath (shape);
            g.setColour (Colours::trayEdge);
            g.strokePath (shape, PathStrokeType (1.0f));
        }
        else if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillPath (shape);
        }
        return;
    }

    if (b.getProperties()["link"])
    {
        if (over && b.isEnabled())
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (r, corner);
        }
        return;
    }

    if (b.getProperties()["primary"] && b.isEnabled())
    {
        g.setGradientFill (ColourGradient (Colours::accent.brighter (over ? 0.15f : 0.0f), r.getX(), r.getY(),
                                           Colours::accent.darker (down ? 0.6f : 0.35f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner + 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
        return;
    }

    g.setGradientFill (ColourGradient (Colour (0xff353a41).brighter (over ? 0.08f : 0.0f), r.getX(), r.getY(),
                                       Colour (0xff1f2227), r.getX(), r.getBottom(), false));
    if (down) g.setColour (Colour (0xff1a1d21));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);

    if (b.getProperties()["led"])
    {
        const float d = 6.0f;
        auto led = Rectangle<float> (d, d).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
        if (on)
        {
            g.setColour (Colours::ledOn.withAlpha (0.35f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (on ? Colours::ledOn : Colours::ledOff);
        g.fillEllipse (led);
    }
}

void Concert7LookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool over, bool)
{
    auto r = b.getLocalBounds();
    if (b.getProperties()["led"]) r.removeFromLeft (16);
    const bool on = b.getToggleState();
    const auto& props = b.getProperties();
    if (props["plain"])
    {
        // dialog buttons: normal sentence case text
        const bool primary = props["primary"] && b.isEnabled();
        const bool link = props["link"];
        g.setFont (uiFont (13.5f, primary));
        g.setColour (! b.isEnabled() ? Colours::textFaint
                     : primary ? Colours::lcdBack
                     : link ? (over ? Colours::lcdOn : Colours::accent)
                     : (over ? juce::Colours::white : Colours::text));
        g.drawFittedText (b.getButtonText(), r.reduced (link ? 8 : 10, 0),
                          link ? Justification::centredLeft : Justification::centred, 1);
        return;
    }

    const float h = props.contains ("fontHeight") ? (float) props["fontHeight"] : 11.5f;
    g.setFont (labelFont (h));
    g.setColour (on ? Colours::text : (over ? Colours::text.withAlpha (0.85f) : Colours::textDim));
    if (! b.isEnabled()) g.setColour (Colours::textFaint);
    g.drawFittedText (b.getButtonText(), r.reduced (4, 0), Justification::centred, 1);
}

void Concert7LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    g.setColour (Colours::panelLight);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (Colours::outline.brighter (over ? 0.3f : 0.0f));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (on)
    {
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (box.reduced (3.0f), 2.0f);
    }
    g.setColour (on ? Colours::text : Colours::textDim);
    g.setFont (labelFont (10.5f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (4), Justification::centredLeft, 1);
}

void Concert7LookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Colours::panelLight.darker (0.2f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Colours::accent : Colours::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    Path arrow;
    const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.isEnabled() ? Colours::accent : Colours::textFaint);
    g.fillPath (arrow);
}

void Concert7LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (Justification::centredLeft);
}

void Concert7LookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::outline);
    g.drawRect (0, 0, w, h);
}

void Concert7LookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                                             bool isHighlighted, bool isTicked, bool hasSubMenu, const String& text,
                                             const String& shortcutKeyText, const Drawable* icon, const Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (Colours::outline);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (2, 0);
    if (isHighlighted && isActive)
    {
        g.setColour (Colours::accentDim);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
    }
    if (isTicked)
    {
        g.setColour (Colours::accent);
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 10.0f, (float) r.getCentreY() }));
    }
    g.setColour (textColour != nullptr ? *textColour : (isActive ? Colours::text : Colours::textFaint));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, r.withTrimmedLeft (22).withTrimmedRight (hasSubMenu ? 20 : 8), Justification::centredLeft, 1);
    if (hasSubMenu)
    {
        Path p;
        const float x = (float) r.getRight() - 12.0f, y = (float) r.getCentreY();
        p.addTriangle (x - 3.0f, y - 4.0f, x - 3.0f, y + 4.0f, x + 2.0f, y);
        g.fillPath (p);
    }
    ignoreUnused (shortcutKeyText, icon);
}

Label* Concert7LookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (12.0f));
    l->setColour (Label::textColourId, Colours::text);
    l->setColour (Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (Label::outlineWhenEditingColourId, Colours::accent);
    l->setColour (Label::backgroundWhenEditingColourId, Colours::lcdBack);
    return l;
}

void Concert7LookAndFeel::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
{
    g.setColour (Colours::panelLight);
    g.fillRoundedRectangle (body, 4.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (body, 4.0f, 1.0f);
}

void Concert7LookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (Colours::panelLight);
    g.setColour (Colours::outline);
    g.drawRect (0, 0, w, h);
    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (text, Rectangle<int> (w, h).reduced (8, 4), Justification::centredLeft, 6);
}

Rectangle<int> Concert7LookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    const int w = std::min (320, GlyphArrangement::getStringWidthInt (uiFont (13.0f), tipText) + 20);
    const int lines = 1 + GlyphArrangement::getStringWidthInt (uiFont (13.0f), tipText) / 300;
    const int h = 10 + lines * 17;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}
void drawLock (Graphics& g, Rectangle<float> area, juce::Colour colour)
{
    const float h = std::min (area.getHeight(), area.getWidth() * 1.3f);
    auto r = area.withSizeKeepingCentre (h / 1.3f, h);
    auto body = r.removeFromBottom (h * 0.55f);
    Path shackle;
    shackle.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.32f, r.getHeight() * 0.85f, 0.0f,
                           -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
    g.setColour (colour);
    g.strokePath (shackle, PathStrokeType (std::max (1.0f, h * 0.12f)));
    g.fillRoundedRectangle (body, h * 0.08f);
}

} // namespace concert7
