#include "Widgets.h"
#include "PluginProcessor.h"

namespace k1t::ui
{
//==============================================================================
static bool hubTicking = false;
float AnimHub::globalTime = 0.0f;

std::vector<Anim*>& AnimHub::list() { static std::vector<Anim*> l; return l; }
double& AnimHub::lastTime() { static double t = 0; return t; }

Anim::Anim() { AnimHub::list().push_back (this); }
Anim::~Anim()
{
    auto& l = AnimHub::list();
    auto it = std::find (l.begin(), l.end(), this);
    if (it == l.end()) return;
    if (hubTicking) *it = nullptr;
    else l.erase (it);
}

void AnimHub::tick()
{
    const double now = Time::getMillisecondCounterHiRes();
    auto& last = lastTime();
    float dt = last == 0 ? 1.0f / 60.0f : (float) ((now - last) / 1000.0);
    if (dt < 0.004f) return;               // plusieurs éditeurs ouverts : on ne double pas la vitesse
    last = now;
    dt = jmin (dt, 0.1f);
    globalTime += dt;

    hubTicking = true;
    auto& l = list();
    for (size_t i = 0; i < l.size(); ++i)
        if (auto* a = l[i]) a->tick (dt);
    hubTicking = false;
    l.erase (std::remove (l.begin(), l.end(), nullptr), l.end());
}

//==============================================================================
// Knob
Knob::Knob (const String& l, double mn, double mx, double def, Colour c)
    : Slider (RotaryHorizontalVerticalDrag, NoTextBox), label (l), colour (c), defaultValue (def)
{
    setRange (mn, mx, 0.0);
    setValue (def, dontSendNotification);
    setDoubleClickReturnValue (true, def);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity (220);
    setScrollWheelEnabled (true);
    bipolar = mn < 0 && mx > 0;
    shown = (float) valueToProportionOfLength (def);
    onValueChange = [this] { if (onChange) onChange ((float) getValue()); };
    format = [] (double v) { return String (v, 1); };
    setPopupMenuEnabled (false);
}

void Knob::tick (float dt)
{
    const float target = (float) valueToProportionOfLength (getValue());
    const float h = isMouseOverOrDragging() ? 1.0f : 0.0f;
    const float ns = approach (shown, target, dt, 16.0f);
    const float nh = approach (hover, h, dt, 12.0f);
    if (std::abs (ns - shown) > 0.0004f || std::abs (nh - hover) > 0.002f)
    {
        shown = ns; hover = nh;
        repaint();
    }
    else if (shown != target || hover != h) { shown = target; hover = h; repaint(); }
}

void Knob::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = 13.0f;
    auto area = b.withTrimmedBottom (labelH);
    const float d = jmin (area.getWidth(), area.getHeight()) - 4.0f;
    const auto c = area.getCentre();
    const float r = d * 0.5f;
    const auto rp = getRotaryParameters();
    const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
    const float aV = a0 + shown * (a1 - a0);
    const float arcR = r - 2.0f;

    const bool useLayer = dim < 0.999f;
    if (useLayer) g.beginTransparencyLayer (dim);

    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0, a0, a1, true);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const float from = bipolar ? a0 + (float) valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    if (std::abs (aV - from) > 0.01f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0, jmin (from, aV), jmax (from, aV), true);
        drawGlowPath (g, val, colour, 3.0f, 0.18f + 0.3f * hover);
        g.setGradientFill (ColourGradient (colour.brighter (0.3f), c.x - r, c.y - r, colour.withRotatedHue (0.06f), c.x + r, c.y + r, false));
        g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float inner = arcR - 5.5f;
    auto disk = Rectangle<float> (inner * 2, inner * 2).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff2c2147).interpolatedWith (colour, 0.08f * hover), c.x, c.y - inner,
                                       Colour (0xff140d22), c.x, c.y + inner, false));
    g.fillEllipse (disk);
    g.setColour (Colours::white.withAlpha (0.09f + 0.06f * hover));
    g.drawEllipse (disk.reduced (0.5f), 1.0f);

    const Point<float> p1 = c.getPointOnCircumference (inner * 0.3f, aV);
    const Point<float> p2 = c.getPointOnCircumference (inner * 0.78f, aV);
    g.setColour (Colours::white.withAlpha (0.92f));
    g.drawLine ({ p1, p2 }, 2.0f);

    auto lr = b.removeFromBottom (labelH + 2).toNearestInt();
    g.setFont (fontSpaced (9.5f, true, 0.08f));
    if (hover < 0.99f)
    {
        g.setColour (col::text3.withAlpha (1.0f - hover));
        g.drawText (label, lr, Justification::centred, false);
    }
    if (hover > 0.01f)
    {
        g.setColour (colour.brighter (0.4f).withAlpha (hover));
        g.drawText (format (getValue()), lr, Justification::centred, false);
    }
    if (useLayer) g.endTransparencyLayer();
}

//==============================================================================
// Switch
void Switch::setOn (bool b, bool notify)
{
    on = b;
    s.target = on ? 1.0f : 0.0f;
    if (notify && onToggle) onToggle (on);
}

void Switch::mouseUp (const MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition())) setOn (! on, true);
}

void Switch::tick (float dt)
{
    const bool m = s.step (dt);
    const float nh = approach (hover, isMouseOver() ? 1.0f : 0.0f, dt, 12.0f);
    if (m || std::abs (nh - hover) > 0.002f) { hover = nh; repaint(); }
}

void Switch::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().withSizeKeepingCentre (30.0f, 18.0f);
    const float t = jlimit (0.0f, 1.2f, s.pos);
    const float r = b.getHeight() * 0.5f;
    g.setColour (Colours::white.withAlpha (0.10f + 0.04f * hover));
    g.fillRoundedRectangle (b, r);
    if (t > 0.01f)
    {
        g.setGradientFill (ColourGradient (colour.withAlpha (jmin (1.0f, t)), b.getX(), 0, colour.withRotatedHue (0.08f).withAlpha (jmin (1.0f, t)), b.getRight(), 0, false));
        g.fillRoundedRectangle (b, r);
        Path p; p.addRoundedRectangle (b, r);
        drawGlowPath (g, p, colour, 1.0f, 0.25f * jmin (1.0f, t));
    }
    const float kx = b.getX() + 2.0f + t * (b.getWidth() - b.getHeight());
    auto knob = Rectangle<float> (kx, b.getY() + 2.0f, b.getHeight() - 4.0f, b.getHeight() - 4.0f);
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillEllipse (knob.translated (0, 1));
    g.setColour (Colours::white);
    g.fillEllipse (knob);
}

//==============================================================================
// Pill
Pill::Pill (const String& text, Style st, Icon i) : Button (text), style (st), ic (i)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void drawIcon (Graphics& g, Pill::Icon ic, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const PathStrokeType st (1.8f, PathStrokeType::curved, PathStrokeType::rounded);
    switch (ic)
    {
        case Pill::play:      g.fillPath (icon::play (r)); break;
        case Pill::stop:      g.fillPath (icon::stop (r)); break;
        case Pill::cross:     g.strokePath (icon::cross (r), st); break;
        case Pill::plus:      g.strokePath (icon::plus (r), st); break;
        case Pill::sparkle:   g.fillPath (icon::sparkle (r)); g.fillPath (icon::sparkle (r.withSizeKeepingCentre (r.getWidth() * 0.4f, r.getHeight() * 0.4f).translated (r.getWidth() * 0.32f, -r.getHeight() * 0.3f))); break;
        case Pill::dice:      g.fillPath (icon::dice (r)); break;
        case Pill::arrowDown: g.strokePath (icon::arrowDown (r), st); break;
        case Pill::folder:    g.strokePath (icon::folder (r), st); break;
        case Pill::reverse:   g.strokePath (icon::reverse (r), st); break;
        case Pill::speaker:   g.strokePath (icon::speaker (r, false), PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded)); break;
        case Pill::speakerOff:g.strokePath (icon::speaker (r, true), PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded)); break;
        case Pill::expand:
        {
            auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.27f);
            const float k = b.getWidth() * 0.38f;
            Path p;
            p.startNewSubPath (b.getX(), b.getY() + k); p.lineTo (b.getX(), b.getY()); p.lineTo (b.getX() + k, b.getY());
            p.startNewSubPath (b.getRight() - k, b.getBottom()); p.lineTo (b.getRight(), b.getBottom()); p.lineTo (b.getRight(), b.getBottom() - k);
            p.startNewSubPath (b.getX(), b.getY()); p.lineTo (b.getX() + k * 1.1f, b.getY() + k * 1.1f);
            p.startNewSubPath (b.getRight(), b.getBottom()); p.lineTo (b.getRight() - k * 1.1f, b.getBottom() - k * 1.1f);
            g.strokePath (p, st);
            break;
        }
        case Pill::refresh:
        {
            auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.26f);
            Path p;
            p.addCentredArc (b.getCentreX(), b.getCentreY(), b.getWidth() * 0.5f, b.getHeight() * 0.5f, 0, 0.5f, MathConstants<float>::twoPi - 0.3f, true);
            g.strokePath (p, st);
            const auto tip = b.getCentre() + Point<float> (std::sin (0.5f), -std::cos (0.5f)) * b.getWidth() * 0.5f;
            Path a;
            a.addTriangle (tip.translated (-4.0f, -3.5f), tip.translated (3.5f, -1.0f), tip.translated (-1.5f, 4.0f));
            g.fillPath (a);
            break;
        }
        case Pill::undo:
        {
            auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.24f);
            Path p;
            p.startNewSubPath (b.getX() + b.getWidth() * 0.12f, b.getY() + b.getHeight() * 0.38f);
            p.lineTo (b.getX() + b.getWidth() * 0.62f, b.getY() + b.getHeight() * 0.38f);
            p.cubicTo (b.getRight() + b.getWidth() * 0.05f, b.getY() + b.getHeight() * 0.38f,
                       b.getRight() + b.getWidth() * 0.05f, b.getBottom() - b.getHeight() * 0.05f,
                       b.getX() + b.getWidth() * 0.55f, b.getBottom() - b.getHeight() * 0.05f);
            p.lineTo (b.getX() + b.getWidth() * 0.3f, b.getBottom() - b.getHeight() * 0.05f);
            p.startNewSubPath (b.getX() + b.getWidth() * 0.34f, b.getY() + b.getHeight() * 0.12f);
            p.lineTo (b.getX() + b.getWidth() * 0.08f, b.getY() + b.getHeight() * 0.38f);
            p.lineTo (b.getX() + b.getWidth() * 0.34f, b.getY() + b.getHeight() * 0.64f);
            g.strokePath (p, st);
            break;
        }
        case Pill::none: default: break;
    }
}

void Pill::tick (float dt)
{
    const float nh = approach (hover, isOver() ? 1.0f : 0.0f, dt, 14.0f);
    const float np = approach (press, isDown() ? 1.0f : 0.0f, dt, 26.0f);
    float ns = -1.0f;
    if (style == primary && isEnabled())
    {
        const float phase = std::fmod (AnimHub::globalTime + (float) (reinterpret_cast<uintptr_t> (this) % 97) * 0.03f, 4.0f);
        if (phase < 1.0f) ns = phase;
    }
    if (std::abs (nh - hover) > 0.002f || std::abs (np - press) > 0.002f || ns >= 0.0f || shimmer >= 0.0f)
    {
        hover = nh; press = np; shimmer = ns;
        repaint();
    }
}

void Pill::paintButton (Graphics& g, bool, bool)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    const float sc = 1.0f - press * 0.05f + hover * 0.015f;
    g.addTransform (AffineTransform::scale (sc, sc, b.getCentreX(), b.getCentreY()));
    const float r = b.getHeight() * 0.5f;
    Path shape; shape.addRoundedRectangle (b, r);
    const float alpha = isEnabled() ? 1.0f : 0.35f;

    Colour fg = col::text;
    if (style == primary)
    {
        drawGlowPath (g, shape, accent, 1.0f, (0.18f + 0.3f * hover) * alpha);
        g.setGradientFill (ColourGradient (accent.withAlpha (alpha), b.getX(), b.getY(), col::magenta.withAlpha (alpha), b.getRight(), b.getBottom(), false));
        g.fillPath (shape);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.3f), b.getX(), b.getY(), Colours::transparentWhite, b.getX(), b.getCentreY(), false));
        g.fillPath (shape);
        if (shimmer >= 0.0f)
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (shape);
            const float x = b.getX() - 60.0f + shimmer * (b.getWidth() + 120.0f);
            Path band;
            band.addQuadrilateral (x, b.getBottom(), x + 22, b.getBottom(), x + 52, b.getY(), x + 30, b.getY());
            g.setColour (Colours::white.withAlpha (0.22f));
            g.fillPath (band);
        }
        fg = Colours::white.withAlpha (alpha);
    }
    else if (style == ghost)
    {
        if (active)
        {
            g.setColour (accent.withAlpha (0.28f));
            g.fillPath (shape);
            drawGlowPath (g, shape, accent, 1.0f, 0.18f);
            g.setColour (accent.withAlpha (0.8f));
            g.strokePath (shape, PathStrokeType (1.0f));
        }
        else
        {
            g.setColour (Colours::white.withAlpha ((0.05f + 0.06f * hover) * alpha));
            g.fillPath (shape);
            g.setColour (Colours::white.withAlpha ((0.11f + 0.1f * hover) * alpha));
            g.strokePath (shape, PathStrokeType (1.0f));
        }
        fg = (active ? Colours::white : col::text.withAlpha (0.85f + 0.15f * hover)).withMultipliedAlpha (alpha);
    }
    else
    {
        if (hover > 0.01f || active)
        {
            g.setColour ((active ? accent.withAlpha (0.25f) : Colours::white.withAlpha (0.08f * hover)));
            g.fillPath (shape);
        }
        fg = (active ? accent.brighter (0.5f) : col::text2.interpolatedWith (col::text, hover)).withMultipliedAlpha (alpha);
    }

    const auto text = getButtonText();
    g.setFont (fontSpaced (textSize, true, 0.06f));
    const float iconS = text.isEmpty() ? jmin (b.getHeight() - 4.0f, 18.0f) : jmin (b.getHeight() - 8.0f, 16.0f);
    if (text.isEmpty())
    {
        drawIcon (g, ic, b.withSizeKeepingCentre (iconS, iconS), fg);
        return;
    }
    const float tw = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);
    const float total = tw + (ic != none ? iconS + 6.0f : 0.0f);
    float x = b.getCentreX() - total * 0.5f;
    if (ic != none)
    {
        drawIcon (g, ic, Rectangle<float> (x, b.getCentreY() - iconS * 0.5f, iconS, iconS), fg);
        x += iconS + 6.0f;
    }
    g.setColour (fg);
    g.drawText (text, Rectangle<float> (x, b.getY(), tw + 2.0f, b.getHeight()), Justification::centredLeft, false);
}

//==============================================================================
// Segmented
Segmented::Segmented (StringArray it, bool lg) : items (std::move (it)), large (lg)
{
    x.stiffness = 300.0f; x.damping = 26.0f;
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Rectangle<float> Segmented::itemBounds (int i) const
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    const float w = b.getWidth() / (float) jmax (1, items.size());
    return { b.getX() + w * (float) i, b.getY(), w, b.getHeight() };
}

void Segmented::setSelected (int i, bool notify)
{
    selected = jlimit (0, items.size() - 1, i);
    x.target = (float) selected;
    if (notify && onChange) onChange (selected);
    repaint();
}

void Segmented::mouseUp (const MouseEvent& e)
{
    for (int i = 0; i < items.size(); ++i)
        if (itemBounds (i).contains (e.position) && i != selected) { setSelected (i, true); break; }
}

void Segmented::mouseMove (const MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < items.size(); ++i) if (itemBounds (i).contains (e.position)) h = i;
    if (h != hoverIdx) { hoverIdx = h; repaint(); }
}

void Segmented::tick (float dt) { if (x.step (dt)) repaint(); }

void Segmented::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float r = b.getHeight() * 0.5f;
    g.setColour (Colours::white.withAlpha (large ? 0.05f : 0.06f));
    g.fillRoundedRectangle (b, r);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f);

    const auto ib = itemBounds (0);
    auto pill = ib.withX (ib.getX() + x.pos * ib.getWidth());
    // léger étirement pendant le mouvement (effet "liquide")
    const float stretch = jmin (12.0f, std::abs (x.vel) * 3.0f);
    pill = pill.withSizeKeepingCentre (pill.getWidth() + stretch, pill.getHeight() - stretch * 0.15f);
    Path pp; pp.addRoundedRectangle (pill, pill.getHeight() * 0.5f);
    if (large)
    {
        drawGlowPath (g, pp, col::violet, 1.0f, 0.3f);
        g.setGradientFill (ColourGradient (col::violet, pill.getX(), pill.getY(), col::magenta.withAlpha (0.9f), pill.getRight(), pill.getBottom(), false));
        g.fillPath (pp);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.28f), 0, pill.getY(), Colours::transparentWhite, 0, pill.getCentreY(), false));
        g.fillPath (pp);
    }
    else
    {
        g.setColour (Colours::white.withAlpha (0.16f));
        g.fillPath (pp);
    }

    g.setFont (large ? fontSpaced (12.5f, true, 0.16f) : fontSpaced (9.0f, true, 0.08f));
    for (int i = 0; i < items.size(); ++i)
    {
        const float closeness = jlimit (0.0f, 1.0f, 1.0f - std::abs (x.pos - (float) i));
        auto c = col::text3.interpolatedWith (col::text2, i == hoverIdx ? 1.0f : 0.0f).interpolatedWith (Colours::white, closeness);
        g.setColour (c);
        g.drawText (items[i], itemBounds (i), Justification::centred, false);
    }
}

//==============================================================================
// WaveView
WaveView::WaveView (K1TProcessor* p) : proc (p) {}

void WaveView::setSample (SamplePtr s, bool animate)
{
    if (s == sample) return;
    sample = std::move (s);
    cacheW = -1;
    reveal = animate ? 0.0f : 1.0f;
    repaint();
}

void WaveView::rebuild()
{
    const int w = jmax (1, getWidth());
    cacheW = w;
    mins.assign ((size_t) w, 0.0f);
    maxs.assign ((size_t) w, 0.0f);
    if (sample == nullptr || sample->length() == 0) return;

    const int len = sample->length();
    const float* l = sample->buf.getReadPointer (0);
    const float* r = sample->buf.getReadPointer (1);
    float peak = 1.0e-6f;
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((int64) x * len / w), b = jmax (a + 1, (int) ((int64) (x + 1) * len / w));
        float mn = 0, mx = 0;
        for (int i = a; i < jmin (b, len); ++i)
        {
            const float v = 0.5f * (l[i] + r[i]);
            mn = jmin (mn, v); mx = jmax (mx, v);
        }
        mins[(size_t) x] = mn; maxs[(size_t) x] = mx;
        peak = jmax (peak, jmax (-mn, mx));
    }
    const float k = 1.0f / peak;
    for (int x = 0; x < w; ++x) { mins[(size_t) x] *= k; maxs[(size_t) x] *= k; }
}

void WaveView::tick (float dt)
{
    bool rp = false;
    if (reveal < 1.0f) { reveal = jmin (1.0f, reveal + dt / 0.55f); rp = true; }

    float h = -1.0f;
    if (proc != nullptr && sample != nullptr && proc->playingSample() == sample.get())
        h = proc->playingPosition();
    const float ha = approach (headAlpha, h >= 0 ? 1.0f : 0.0f, dt, 10.0f);
    if (h >= 0) { if (std::abs (h - head) > 0.0005f) rp = true; head = h; }
    if (std::abs (ha - headAlpha) > 0.003f) rp = true;
    headAlpha = ha;
    if (rp) repaint();
}

void WaveView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (sample == nullptr)
    {
        if (emptyText.isNotEmpty())
        {
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText (emptyText, b, Justification::centred, true);
        }
        return;
    }
    if (cacheW != getWidth()) rebuild();

    const float mid = b.getCentreY(), half = b.getHeight() * 0.5f * 0.92f;
    const float ease = 1.0f - std::pow (1.0f - reveal, 3.0f);
    const int w = (int) mins.size();
    const int shownW = jmax (1, (int) (w * ease));

    Path p;
    p.startNewSubPath (0, mid);
    for (int x = 0; x < shownW; ++x) p.lineTo ((float) x, mid - jmax (0.012f, maxs[(size_t) x]) * half);
    for (int x = shownW - 1; x >= 0; --x) p.lineTo ((float) x, mid - jmin (-0.012f, mins[(size_t) x]) * half);
    p.closeSubPath();

    ColourGradient grad (colour.withAlpha (0.95f), 0, b.getY(), colour.withAlpha (0.95f), 0, b.getBottom(), false);
    grad.addColour (0.5, colour.withAlpha (0.35f));
    g.setGradientFill (grad);
    g.fillPath (p);
    if (! compact)
    {
        g.setColour (colour.brighter (0.4f).withAlpha (0.55f));
        g.strokePath (p, PathStrokeType (1.0f));
    }

    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) mid, 0, b.getWidth());

    if (ease < 1.0f)
    {
        const float x = (float) shownW;
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.6f), x, mid, Colours::transparentWhite, x - 30, mid, false));
        g.fillRect (Rectangle<float> (x - 30, b.getY(), 30, b.getHeight()));
    }

    if (headAlpha > 0.01f && head >= 0.0f)
    {
        const float x = head * b.getWidth();
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (Rectangle<float> (0, 0, x, b.getHeight()).toNearestInt());
            g.setColour (Colours::white.withAlpha (0.35f * headAlpha));
            g.fillPath (p);
        }
        g.setColour (Colours::white.withAlpha (0.9f * headAlpha));
        g.fillRect (Rectangle<float> (x - 0.75f, b.getY(), 1.5f, b.getHeight()));
        g.setColour (colour.withAlpha (0.3f * headAlpha));
        g.fillRect (Rectangle<float> (x - 4.0f, b.getY(), 8.0f, b.getHeight()));
    }
}

void WaveView::mouseUp (const MouseEvent& e)
{
    if (! dragging && e.mouseWasClicked() && onClick) onClick();
    dragging = false;
}

void WaveView::mouseDrag (const MouseEvent& e)
{
    if (! allowDragOut || dragging || sample == nullptr || proc == nullptr) return;
    if (e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    dragSampleOut (*proc, *sample, dragName.isNotEmpty() ? dragName : sample->name, this);
}

void dragSampleOut (K1TProcessor& p, const SampleData& s, const String& name, Component* source)
{
    const auto f = p.exportForDrag (s, name);
    if (f.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, source);
}

//==============================================================================
// Toast
void Toast::show (const String& msg, Colour a)
{
    text = msg;
    accent = a;
    y.target = 1.0f;
    y.stiffness = 220.0f; y.damping = 20.0f;
    hideAt = Time::getMillisecondCounterHiRes() + 2800.0;
    toFront (false);
    repaint();
}

void Toast::tick (float dt)
{
    if (y.target > 0.5f && Time::getMillisecondCounterHiRes() > hideAt) y.target = 0.0f;
    if (y.step (dt)) repaint (getLocalBounds().removeFromBottom (140));
}

void Toast::paint (Graphics& g)
{
    if (y.pos < 0.005f || text.isEmpty()) return;
    g.setFont (font (13.0f, true));
    const float tw = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);
    const float w = tw + 56.0f, h = 40.0f;
    const float cy = (float) getHeight() + 30.0f - y.pos * 96.0f;
    auto r = Rectangle<float> (w, h).withCentre ({ getWidth() * 0.5f, cy });
    const float a = jlimit (0.0f, 1.0f, y.pos);

    Path p; p.addRoundedRectangle (r, h * 0.5f);
    drawGlowPath (g, p, accent, 1.0f, 0.35f * a);
    g.setColour (Colour (0xff1c1330).withAlpha (0.96f * a));
    g.fillPath (p);
    g.setColour (accent.withAlpha (0.55f * a));
    g.strokePath (p, PathStrokeType (1.0f));
    g.setColour (accent.withAlpha (a));
    g.fillPath (icon::sparkle (Rectangle<float> (14, 14).withCentre ({ r.getX() + 22.0f, r.getCentreY() })));
    g.setColour (col::text.withAlpha (a));
    g.drawText (text, r.withTrimmedLeft (38).withTrimmedRight (14), Justification::centredLeft, false);
}

//==============================================================================
// Particles
void Particles::burst (Point<float> at, int count, Colour base)
{
    auto& rnd = Random::getSystemRandom();
    const Colour palette[] = { base, col::magenta, col::lilac, Colours::white, col::pink, col::indigo };
    for (int i = 0; i < count; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi;
        const float sp = 90.0f + rnd.nextFloat() * 360.0f;
        P p;
        p.pos = at;
        p.vel = { std::cos (a) * sp, std::sin (a) * sp - 80.0f };
        p.maxLife = p.life = 0.55f + rnd.nextFloat() * 0.75f;
        p.size = 2.0f + rnd.nextFloat() * 5.0f;
        p.rot = rnd.nextFloat() * 6.28f;
        p.spin = (rnd.nextFloat() - 0.5f) * 10.0f;
        p.c = palette[(size_t) rnd.nextInt (6)];
        p.star = rnd.nextFloat() < 0.45f;
        ps.push_back (p);
    }
}

void Particles::tick (float dt)
{
    if (ps.empty()) return;
    Rectangle<float> dirty;
    for (auto& p : ps) dirty = dirty.getUnion (Rectangle<float> (24, 24).withCentre (p.pos));
    const float damp = std::pow (0.9f, dt * 60.0f);
    for (auto& p : ps)
    {
        p.pos += p.vel * dt;
        p.vel *= damp;
        p.vel.y += 260.0f * dt;
        p.life -= dt;
        p.rot += p.spin * dt;
        dirty = dirty.getUnion (Rectangle<float> (24, 24).withCentre (p.pos));
    }
    ps.erase (std::remove_if (ps.begin(), ps.end(), [] (const P& p) { return p.life <= 0; }), ps.end());
    repaint (dirty.getSmallestIntegerContainer());
}

void Particles::paint (Graphics& g)
{
    for (auto& p : ps)
    {
        const float a = jlimit (0.0f, 1.0f, p.life / p.maxLife);
        g.setColour (p.c.withAlpha (a));
        if (p.star)
        {
            auto path = icon::sparkle (Rectangle<float> (p.size * 2.6f, p.size * 2.6f).withCentre ({}));
            path.applyTransform (AffineTransform::rotation (p.rot).translated (p.pos));
            g.fillPath (path);
        }
        else
            g.fillEllipse (Rectangle<float> (p.size, p.size).withCentre (p.pos));
    }
}

} // namespace k1t::ui
