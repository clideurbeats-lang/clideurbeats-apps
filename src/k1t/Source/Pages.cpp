#include <set>
#include <complex>
#include "Pages.h"
#include "PluginProcessor.h"

namespace k1t::ui
{
//==============================================================================
static String fmtDb (double v)  { return (v >= 0 ? "+" : "") + String (v, 1) + " dB"; }
static String fmtSt (double v)  { return (v >= 0 ? "+" : "") + String (v, 1) + " st"; }
static String fmtMs (double v)  { return (v >= 0 ? "+" : "") + String (v, 1) + " ms"; }
static String fmtPct (double v) { return String (roundToInt (v * 100.0)) + " %"; }
static String fmtBi (double v)  { return (v >= 0 ? "+" : "") + String (roundToInt (v * 100.0)); }
static String fmtPan (double v)
{
    if (std::abs (v) < 0.02) return "C";
    return (v < 0 ? "L " : "R ") + String (roundToInt (std::abs (v) * 100.0));
}
static String fmtHz (double v)
{
    return v >= 1000.0 ? String (v / 1000.0, 1) + " kHz" : String (roundToInt (v)) + " Hz";
}

bool isAudioFile (const String& path)
{
    return File (path).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a");
}

static void dashedRect (Graphics& g, Rectangle<float> r, float radius, float offset, Colour c, float w = 1.5f)
{
    Path p; p.addRoundedRectangle (r, radius);
    constexpr float on = 6.0f, period = 11.0f;
    Path dashes;
    PathFlatteningIterator it (p, {}, 0.5f);
    float dist = std::fmod (offset, period);
    while (it.next())
    {
        const Point<float> a (it.x1, it.y1), b (it.x2, it.y2);
        const float len = a.getDistanceFrom (b);
        float t = 0.0f;
        while (t < len)
        {
            const float phase = std::fmod (dist + t, period);
            const bool drawing = phase < on;
            const float step = jmin (len - t, drawing ? on - phase : period - phase);
            if (drawing)
            {
                auto p1 = a + (b - a) * (t / len), p2 = a + (b - a) * ((t + step) / len);
                dashes.startNewSubPath (p1); dashes.lineTo (p2);
            }
            t += jmax (0.01f, step);
        }
        dist += len;
    }
    g.setColour (c);
    g.strokePath (dashes, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
// LayerCard
LayerCard::LayerCard (Ctx& c, int i, std::function<void()> ch)
    : ctx (c), index (i), changed (std::move (ch)), wave (&c.p),
      vol ("VOL", -24, 12, 0, col::layer (i)), pitch ("PITCH", -12, 12, 0, col::layer (i)),
      nudge ("NUDGE", -25, 25, 0, col::layer (i)), pan ("PAN", -1, 1, 0, col::layer (i)),
      decay ("DECAY", 0.02, 1, 1, col::layer (i)), tone ("TONE", -1, 1, 0, col::layer (i))
{
    wave.colour = col::layer (i);
    wave.compact = true;
    wave.allowDragOut = true;
    wave.onClick = [this] { ctx.p.preview (ctx.p.layers[(size_t) index].src); };
    addAndMakeVisible (wave);

    vol.format = fmtDb;   vol.onChange   = [this] (float v) { ctx.p.layers[(size_t) index].gainDb = v; changed(); };
    pitch.format = fmtSt; pitch.onChange = [this] (float v) { ctx.p.layers[(size_t) index].pitch = v; changed(); };
    nudge.format = fmtMs; nudge.onChange = [this] (float v) { ctx.p.layers[(size_t) index].nudgeMs = v; changed(); };
    pan.format = fmtPan;  pan.onChange   = [this] (float v) { ctx.p.layers[(size_t) index].pan = v; changed(); };
    decay.format = fmtPct; decay.onChange = [this] (float v) { ctx.p.layers[(size_t) index].decay = v; changed(); };
    tone.format = [] (double v) { return std::abs (v) < 0.02 ? String ("neutre") : (v < 0 ? "sombre " : "brillant ") + String (roundToInt (std::abs (v) * 100)); };
    tone.onChange = [this] (float v) { ctx.p.layers[(size_t) index].tone = v; changed(); };

    for (auto* k : { &vol, &pitch, &nudge, &pan, &decay, &tone }) addAndMakeVisible (*k);

    play.onClick = [this] { ctx.p.preview (ctx.p.layers[(size_t) index].src); };
    mute.onClick = [this]
    {
        auto& l = ctx.p.layers[(size_t) index];
        l.mute = ! l.mute;
        refresh();
        changed();
    };
    rev.onClick = [this]
    {
        auto& l = ctx.p.layers[(size_t) index];
        l.reverse = ! l.reverse;
        refresh();
        changed();
    };
    remove.onClick = [this]
    {
        ctx.p.clearLayer (index);
        refresh();
        changed();
    };
    mute.accent = rev.accent = col::layer (i);
    for (auto* b : { &play, &mute, &rev, &remove }) { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); }
    setRepaintsOnMouseActivity (true);
    refresh();
}

void LayerCard::refresh()
{
    const auto& L = ctx.p.layers[(size_t) index];
    const bool loaded = L.src != nullptr;
    if (wave.getSample() != L.src) appear = 0.0f;
    wave.setSample (L.src);
    wave.dragName = L.src != nullptr ? L.src->name : String();
    vol.setValueQuiet (L.gainDb);  pitch.setValueQuiet (L.pitch);
    nudge.setValueQuiet (L.nudgeMs); pan.setValueQuiet (L.pan);
    decay.setValueQuiet (L.decay); tone.setValueQuiet (L.tone);
    mute.setIcon (L.mute ? Pill::speakerOff : Pill::speaker);
    mute.setActive (L.mute);
    rev.setActive (L.reverse);
    for (Component* c : std::initializer_list<Component*> { &wave, &vol, &pitch, &nudge, &pan, &decay, &tone, &play, &mute, &rev, &remove })
        c->setVisible (loaded);
    const float d = L.mute ? 0.4f : 1.0f;
    for (auto* k : { &vol, &pitch, &nudge, &pan, &decay, &tone }) { k->dim = d; k->repaint(); }
    wave.setAlpha (L.mute ? 0.35f : 1.0f);
    repaint();
}

void LayerCard::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (26).expanded (0, 2);
    remove.setBounds (top.removeFromRight (30));
    mute.setBounds (top.removeFromRight (30));
    rev.setBounds (top.removeFromRight (30));
    play.setBounds (top.removeFromRight (30));
    r.removeFromTop (4);
    wave.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    const int kw = r.getWidth() / 6;
    for (auto* k : { &vol, &pitch, &nudge, &pan, &decay, &tone })
        k->setBounds (r.removeFromLeft (kw));
}

void LayerCard::tick (float dt)
{
    const float nh = approach (hover, isMouseOver (true) ? 1.0f : 0.0f, dt, 10.0f);
    const float nd = approach (drop, dragOver ? 1.0f : 0.0f, dt, 14.0f);
    const float na = approach (appear, 1.0f, dt, 7.0f);
    const bool empty = ctx.p.layers[(size_t) index].src == nullptr;
    const bool animDash = empty && (hover > 0.01f || drop > 0.01f);
    if (animDash) dash += dt * 18.0f;
    if (std::abs (nh - hover) > 0.002f || std::abs (nd - drop) > 0.002f || std::abs (na - appear) > 0.002f || animDash)
    {
        hover = nh; drop = nd; appear = na;
        repaint();
    }
}

void LayerCard::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto c = col::layer (index);
    const auto& L = ctx.p.layers[(size_t) index];
    const float lift = jmax (hover, drop);

    if (L.src == nullptr)
    {
        g.setColour (Colours::white.withAlpha (0.02f + 0.03f * hover + 0.05f * drop));
        g.fillRoundedRectangle (b, 18.0f);
        dashedRect (g, b.reduced (1.0f), 18.0f, dash, c.withAlpha (0.25f + 0.5f * lift), 1.4f);

        const auto ctr = b.getCentre().translated (0, -10);
        const float pr = 15.0f + 3.0f * drop;
        auto circle = Rectangle<float> (pr * 2, pr * 2).withCentre (ctr);
        g.setColour (c.withAlpha (0.15f + 0.2f * lift));
        g.fillEllipse (circle);
        g.setColour (c.withAlpha (0.7f + 0.3f * lift));
        g.strokePath (icon::plus (circle), PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setFont (font (12.0f, true));
        g.setColour (col::text2.interpolatedWith (col::text, lift));
        g.drawText (drop > 0.5f ? String::fromUTF8 ("Lâche-le !") : "Glisse un sample ou clique",
                    b.withTrimmedTop (b.getHeight() * 0.5f + 14).withHeight (18), Justification::centred);
        g.setFont (fontSpaced (9.5f, true, 0.2f));
        g.setColour (col::text3);
        g.drawText ("LAYER " + String (index + 1).paddedLeft ('0', 2), b.withTrimmedTop (12).withHeight (14).withTrimmedLeft (16), Justification::centredLeft);
        return;
    }

    drawCard (g, b, 18.0f, lift, c);

    // pastille de couleur + nom
    auto top = getLocalBounds().reduced (12, 10).removeFromTop (24).toFloat();
    const float pulse = 0.5f + 0.5f * std::sin (AnimHub::globalTime * 3.0f + index);
    auto dot = Rectangle<float> (8, 8).withCentre ({ top.getX() + 5, top.getCentreY() });
    g.setColour (c.withAlpha (0.25f + 0.2f * pulse));
    g.fillEllipse (dot.expanded (3));
    g.setColour (c);
    g.fillEllipse (dot);
    g.setFont (fontSpaced (10.0f, true, 0.12f));
    g.setColour (c);
    g.drawText (String (index + 1).paddedLeft ('0', 2), top.withTrimmedLeft (16).withWidth (22), Justification::centredLeft);
    g.setFont (font (13.0f, true));
    g.setColour (col::text.withAlpha (appear));
    g.drawText (L.src->name, top.withTrimmedLeft (40).withTrimmedRight (108), Justification::centredLeft, true);

    if (drop > 0.01f)
    {
        g.setColour (c.withAlpha (0.2f * drop));
        g.fillRoundedRectangle (b, 18.0f);
        g.setColour (Colours::white.withAlpha (drop));
        g.setFont (font (13.0f, true));
        g.drawText ("Remplacer", b, Justification::centred);
    }
}

void LayerCard::mouseUp (const MouseEvent& e)
{
    if (ctx.p.layers[(size_t) index].src == nullptr && e.mouseWasClicked()) browse();
}

void LayerCard::browse()
{
    chooser = std::make_unique<FileChooser> ("Choisis un ou plusieurs samples", File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                          [this] (const FileChooser& fc)
                          {
                              StringArray paths;
                              for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                              if (! paths.isEmpty()) filesDropped (paths, 0, 0);
                          });
}

bool LayerCard::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}

void LayerCard::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    if (auto* page = findParentComponentOfClass<MixPage>())
        page->addFiles (files, index);
}

//==============================================================================
// TimingLane
TimingLane::TimingLane (Ctx& c, std::function<void()> ch) : ctx (c), changed (std::move (ch))
{
    for (int i = 0; i < 4; ++i) { shownNudge[(size_t) i] = ctx.p.layers[(size_t) i].nudgeMs; shownFinal[(size_t) i] = shownNudge[(size_t) i]; }
}

Rectangle<float> TimingLane::laneArea() const
{
    return getLocalBounds().toFloat().reduced (16, 0).withTrimmedTop (40).withTrimmedBottom (24).withTrimmedLeft (26);
}
float TimingLane::msToX (float ms) const { auto a = laneArea(); return jmap (ms, -25.0f, 25.0f, a.getX(), a.getRight()); }
float TimingLane::xToMs (float x) const { auto a = laneArea(); return jlimit (-25.0f, 25.0f, jmap (x, a.getX(), a.getRight(), -25.0f, 25.0f)); }
float TimingLane::laneY (int i) const { auto a = laneArea(); return a.getY() + a.getHeight() * ((float) i + 0.5f) / 4.0f; }

int TimingLane::dotAt (Point<float> p) const
{
    int best = -1; float bd = 14.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (ctx.p.layers[(size_t) i].src == nullptr) continue;
        const float d = p.getDistanceFrom ({ msToX (shownNudge[(size_t) i]), laneY (i) });
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) // clic n'importe où sur la ligne
        for (int i = 0; i < 4; ++i)
            if (ctx.p.layers[(size_t) i].src != nullptr && std::abs (p.y - laneY (i)) < laneArea().getHeight() / 8.0f && laneArea().expanded (8, 0).contains (p))
                return i;
    return best;
}

void TimingLane::mouseMove (const MouseEvent& e)
{
    const int h = dotAt (e.position);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void TimingLane::mouseDown (const MouseEvent& e)
{
    dragging = dotAt (e.position);
    mouseDrag (e);
}

void TimingLane::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    float ms = xToMs (e.position.x);
    if (! e.mods.isShiftDown()) ms = std::round (ms * 10.0f) / 10.0f;
    if (std::abs (ms) < 0.35f) ms = 0.0f; // aimant sur 0
    auto& L = ctx.p.layers[(size_t) dragging];
    if (L.nudgeMs != ms) { L.nudgeMs = ms; changed(); }
}

void TimingLane::mouseDoubleClick (const MouseEvent& e)
{
    const int i = dotAt (e.position);
    if (i >= 0) { ctx.p.layers[(size_t) i].nudgeMs = 0; changed(); }
}

void TimingLane::tick (float dt)
{
    bool rp = false;
    for (int i = 0; i < 4; ++i)
    {
        const auto& L = ctx.p.layers[(size_t) i];
        const float fin = L.nudgeMs + ctx.p.spreadMs * spreadFactor (ctx.p.spreadSeed, i);
        const float a = approach (shownNudge[(size_t) i], L.nudgeMs, dt, 18.0f);
        const float b = approach (shownFinal[(size_t) i], fin, dt, 9.0f);
        if (std::abs (a - shownNudge[(size_t) i]) > 0.002f || std::abs (b - shownFinal[(size_t) i]) > 0.002f) rp = true;
        shownNudge[(size_t) i] = a; shownFinal[(size_t) i] = b;
    }
    if (rp) repaint();
}

void TimingLane::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawCard (g, b, 18.0f);
    sectionTitle (g, getLocalBounds().reduced (16, 0).removeFromTop (34).withTrimmedTop (6), String::fromUTF8 ("TIMING  ·  MICRO-DÉCALAGE"));
    g.setFont (font (10.5f));
    g.setColour (col::text3);
    g.drawText (String::fromUTF8 ("glisse les points  ·  shift = fin"), getLocalBounds().reduced (16, 0).removeFromTop (34).withTrimmedTop (6), Justification::centredRight);

    const auto a = laneArea();
    // grille
    for (int ms = -20; ms <= 20; ms += 5)
    {
        const float x = msToX ((float) ms);
        g.setColour (Colours::white.withAlpha (ms == 0 ? 0.0f : (ms % 10 == 0 ? 0.07f : 0.035f)));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        if (ms % 10 == 0)
        {
            g.setFont (font (9.0f));
            g.setColour (col::text3);
            g.drawText ((ms > 0 ? "+" : "") + String (ms), Rectangle<float> (x - 16, a.getBottom() + 4, 32, 12), Justification::centred);
        }
    }
    // ligne zéro (le "temps" de la note)
    {
        const float x = msToX (0);
        g.setGradientFill (ColourGradient (col::violetHi.withAlpha (0.0f), x, a.getY() - 6, col::violetHi.withAlpha (0.0f), x, a.getBottom() + 6, false));
        ColourGradient cg (col::violetHi.withAlpha (0.05f), x, a.getY() - 6, col::violetHi.withAlpha (0.05f), x, a.getBottom() + 6, false);
        cg.addColour (0.5, col::violetHi.withAlpha (0.7f));
        g.setGradientFill (cg);
        g.fillRect (Rectangle<float> (x - 0.75f, a.getY() - 6, 1.5f, a.getHeight() + 12));
    }

    for (int i = 0; i < 4; ++i)
    {
        const auto& L = ctx.p.layers[(size_t) i];
        const float y = laneY (i);
        const auto c = col::layer (i);
        g.setFont (fontSpaced (9.0f, true, 0.1f));
        g.setColour (L.src != nullptr ? c.withAlpha (0.9f) : col::text3.withAlpha (0.4f));
        g.drawText (String (i + 1).paddedLeft ('0', 2), Rectangle<float> (16, y - 7, 22, 14), Justification::centredLeft);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (a.getX(), y - 1.0f, a.getWidth(), 2.0f, 1.0f);
        if (L.src == nullptr) continue;

        const float alpha = L.mute ? 0.3f : 1.0f;
        const float xn = msToX (shownNudge[(size_t) i]);
        const float xf = msToX (shownFinal[(size_t) i]);

        // barre "présence" depuis le zéro
        g.setColour (c.withAlpha (0.22f * alpha));
        g.fillRoundedRectangle (Rectangle<float> (jmin (xn, msToX (0)), y - 3, std::abs (xn - msToX (0)), 6), 3);

        if (std::abs (xf - xn) > 0.6f)
        {
            g.setColour (c.withAlpha (0.45f * alpha));
            Path line; line.startNewSubPath (xn, y); line.lineTo (xf, y);
            Path dashed; const float dl[] = { 2.0f, 3.0f };
            PathStrokeType (1.2f).createDashedStroke (dashed, line, dl, 2);
            g.fillPath (dashed);
            g.drawEllipse (Rectangle<float> (9, 9).withCentre ({ xf, y }), 1.2f);
        }

        const bool hot = (i == hovered || i == dragging);
        const float r = hot ? 8.0f : 6.5f;
        Path dot; dot.addEllipse (Rectangle<float> (r * 2, r * 2).withCentre ({ xn, y }));
        drawGlowPath (g, dot, c, 1.0f, (hot ? 0.45f : 0.25f) * alpha);
        g.setColour (c.withAlpha (alpha));
        g.fillPath (dot);
        g.setColour (Colours::white.withAlpha (0.85f * alpha));
        g.fillEllipse (Rectangle<float> (r * 0.7f, r * 0.7f).withCentre ({ xn - r * 0.2f, y - r * 0.2f }));

        if (hot || std::abs (L.nudgeMs) > 0.05f)
        {
            g.setFont (font (10.0f, true));
            g.setColour ((hot ? Colours::white : c).withAlpha (alpha));
            const float tx = xn + (xn > a.getCentreX() ? -58.0f : 12.0f);
            g.drawText (fmtMs (L.nudgeMs), Rectangle<float> (tx, y - 16, 48, 12), xn > a.getCentreX() ? Justification::centredRight : Justification::centredLeft);
        }
    }
}

//==============================================================================
// EQ graphique
EqGraph::EqGraph (Ctx& c, std::function<void()> ch, bool lg) : ctx (c), changed (std::move (ch)), large (lg)
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (MouseCursor::CrosshairCursor);
}

Rectangle<float> EqGraph::plot() const
{
    auto r = getLocalBounds().toFloat();
    return large ? r.withTrimmedLeft (40).withTrimmedBottom (22).withTrimmedTop (6).withTrimmedRight (10)
                 : r.reduced (2.0f, 2.0f);
}

float EqGraph::fToX (float f) const
{
    auto p = plot();
    return p.getX() + p.getWidth() * (std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f));
}
float EqGraph::xToF (float x) const
{
    auto p = plot();
    return jlimit (20.0f, 20000.0f, 20.0f * std::pow (1000.0f, (x - p.getX()) / p.getWidth()));
}
float EqGraph::dbToY (float db) const { auto p = plot(); return jmap (db, range, -range, p.getY(), p.getBottom()); }
float EqGraph::yToDb (float y) const { auto p = plot(); return jlimit (-range, range, jmap (y, p.getY(), p.getBottom(), range, -range)); }

Point<float> EqGraph::nodePos (int i) const
{
    const auto& b = ctx.p.fx.eq[(size_t) i];
    return { fToX (b.freq), dbToY (b.gain) };
}

int EqGraph::nodeAt (Point<float> p) const
{
    const float rad = large ? 14.0f : 10.0f;
    int best = -1;
    float bd = rad;
    for (int i = 0; i < FxParams::kEqBands; ++i)
    {
        const float d = nodePos (i).getDistanceFrom (p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

float EqGraph::responseDb (float f) const
{
    constexpr double sr = 44100.0;
    const double w = MathConstants<double>::twoPi * jmin ((double) f, sr * 0.499) / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    double mag = 1.0;
    for (int i = 0; i < FxParams::kEqBands; ++i)
    {
        const auto& b = ctx.p.fx.eq[(size_t) i];
        if (std::abs (b.gain) < 0.01f) continue;
        const auto c = FxParams::eqCoefficients (i, b, sr);
        const auto num = (double) c.coefficients[0] + (double) c.coefficients[1] * z1 + (double) c.coefficients[2] * z2;
        const auto den = 1.0 + (double) c.coefficients[3] * z1 + (double) c.coefficients[4] * z2;
        mag *= std::abs (num / den);
    }
    return (float) Decibels::gainToDecibels (mag, -60.0);
}

void EqGraph::setSpectrum (const SampleData* s)
{
    spectrum.clear();
    if (s == nullptr || s->length() < 64) { repaint(); return; }
    constexpr int order = 12, N = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> acc (N / 2 + 1, 0.0f), buf ((size_t) N * 2);
    const int len = jmin (s->length(), (int) (s->sr * 1.0));
    const int hop = N / 2;
    int frames = 0;
    for (int start = 0; start < jmax (1, len - N / 4); start += hop)
    {
        for (int i = 0; i < N; ++i)
        {
            const int k = start + i;
            const float wv = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) N);
            buf[(size_t) i] = k < len ? 0.5f * (s->buf.getSample (0, k) + s->buf.getSample (1, k)) * wv : 0.0f;
        }
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= N / 2; ++k) acc[(size_t) k] += buf[(size_t) k] * buf[(size_t) k];
        ++frames;
        if (frames > 24) break;
    }
    constexpr int bins = 160;
    spectrum.assign (bins, 0.0f);
    float mx = -200.0f;
    std::vector<float> db (bins);
    for (int j = 0; j < bins; ++j)
    {
        const float f0 = 20.0f * std::pow (1000.0f, (float) j / bins), f1 = 20.0f * std::pow (1000.0f, (float) (j + 1) / bins);
        int k0 = (int) (f0 * N / s->sr), k1 = jmax (k0 + 1, (int) (f1 * N / s->sr));
        k0 = jlimit (1, N / 2, k0); k1 = jlimit (k0 + 1, N / 2 + 1, k1);
        double e = 0;
        for (int k = k0; k < k1; ++k) e += acc[(size_t) k];
        e /= (k1 - k0);
        // pente +3 dB/oct (lecture "à l'oreille", comme les analyseurs pro)
        db[(size_t) j] = 10.0f * std::log10 ((float) e + 1.0e-12f) + 3.0f * std::log2 (jmax (20.0f, f0) / 1000.0f);
        mx = jmax (mx, db[(size_t) j]);
    }
    for (int j = 0; j < bins; ++j)
    {
        const float sm = (db[(size_t) jmax (0, j - 1)] + 2 * db[(size_t) j] + db[(size_t) jmin (bins - 1, j + 1)]) * 0.25f;
        spectrum[(size_t) j] = jlimit (0.0f, 1.0f, (sm - (mx - 60.0f)) / 60.0f);
    }
    repaint();
}

void EqGraph::tick (float dt)
{
    bool dirty = false;
    for (int i = 0; i < FxParams::kEqBands; ++i)
    {
        const float t = (i == hovered || i == dragging) ? 1.0f : 0.0f;
        const float n = approach (nodeHover[(size_t) i], t, dt, 16.0f);
        if (std::abs (n - nodeHover[(size_t) i]) > 0.002f) { nodeHover[(size_t) i] = n; dirty = true; }
    }
    if (dirty) repaint();
}

void EqGraph::edited()
{
    if (! ctx.p.fx.on[fxEq]) ctx.p.fx.on[fxEq] = true;
    if (changed) changed();
    repaint();
}

void EqGraph::mouseMove (const MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hovered) { hovered = h; setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor); repaint(); }
}

void EqGraph::mouseExit (const MouseEvent&) { if (dragging < 0) { hovered = -1; repaint(); } }

void EqGraph::mouseDown (const MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging < 0)
    {
        // clic dans le vide : on attrape la bande la plus proche en fréquence et on la place là
        const float f = xToF (e.position.x);
        float best = 1.0e9f;
        for (int i = 0; i < FxParams::kEqBands; ++i)
        {
            const float d = std::abs (std::log (ctx.p.fx.eq[(size_t) i].freq / f));
            if (d < best) { best = d; dragging = i; }
        }
        auto& b = ctx.p.fx.eq[(size_t) dragging];
        b.freq = f;
        b.gain = yToDb (e.position.y);
        edited();
    }
    hovered = dragging;
    lastPos = e.position;
}

void EqGraph::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    auto d = e.position - lastPos;
    lastPos = e.position;
    const float k = e.mods.isShiftDown() ? 0.12f : 1.0f; // Shift = réglage fin
    auto& b = ctx.p.fx.eq[(size_t) dragging];
    if (! e.mods.isCommandDown()) b.freq = xToF (fToX (b.freq) + d.x * k);   // Cmd = gain seulement
    if (! e.mods.isAltDown())     b.gain = yToDb (dbToY (b.gain) + d.y * k); // Alt = fréquence seulement
    edited();
}

void EqGraph::mouseUp (const MouseEvent& e)
{
    dragging = -1;
    hovered = nodeAt (e.position);
    repaint();
}

void EqGraph::mouseDoubleClick (const MouseEvent& e)
{
    const int i = nodeAt (e.position);
    if (i < 0) return;
    const FxParams d;
    auto& b = ctx.p.fx.eq[(size_t) i];
    b.gain = 0.0f;
    b.q = d.eq[(size_t) i].q;
    edited();
}

void EqGraph::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    int i = hovered >= 0 ? hovered : nodeAt (e.position);
    if (i < 0) { Component::mouseWheelMove (e, w); return; }
    auto& b = ctx.p.fx.eq[(size_t) i];
    const bool shelf = i == 0 || i == FxParams::kEqBands - 1;
    const float k = e.mods.isShiftDown() ? 0.25f : 1.0f;
    b.q = jlimit (shelf ? 0.3f : 0.2f, shelf ? 2.0f : 12.0f, b.q * std::exp ((w.deltaY + w.deltaX) * 2.0f * k));
    edited();
}

static String fmtFreq (float f)
{
    return f >= 1000.0f ? String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (f)) + " Hz";
}

void EqGraph::paint (Graphics& g)
{
    auto p = plot();
    const bool on = ctx.p.fx.on[fxEq];
    const float alpha = on ? 1.0f : 0.5f;

    g.setColour (Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (p.expanded (large ? 0.0f : 1.0f), large ? 12.0f : 9.0f);

    // grille
    g.setFont (font (large ? 10.0f : 8.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = fToX (f);
        g.setColour (Colours::white.withAlpha (f == 100.0f || f == 1000.0f || f == 10000.0f ? 0.08f : 0.04f));
        g.fillRect (x, p.getY(), 1.0f, p.getHeight());
        if (large)
        {
            g.setColour (col::text3);
            g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)),
                        Rectangle<float> (x - 20, p.getBottom() + 4, 40, 14), Justification::centred);
        }
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = dbToY (db);
        g.setColour (Colours::white.withAlpha (db == 0.0f ? 0.12f : 0.04f));
        g.fillRect (p.getX(), y, p.getWidth(), 1.0f);
        if (large)
        {
            g.setColour (col::text3);
            g.drawText ((db > 0 ? "+" : "") + String (roundToInt (db)), Rectangle<float> (p.getX() - 38, y - 7, 32, 14), Justification::centredRight);
        }
    }

    // spectre du son
    if (! spectrum.empty())
    {
        Path sp;
        sp.startNewSubPath (p.getX(), p.getBottom());
        const int n = (int) spectrum.size();
        for (int j = 0; j < n; ++j)
            sp.lineTo (p.getX() + p.getWidth() * ((float) j + 0.5f) / (float) n, p.getBottom() - p.getHeight() * 0.85f * spectrum[(size_t) j]);
        sp.lineTo (p.getRight(), p.getBottom());
        sp.closeSubPath();
        g.setGradientFill (ColourGradient (col::indigo.withAlpha (0.22f), 0, p.getY(), col::indigo.withAlpha (0.03f), 0, p.getBottom(), false));
        g.fillPath (sp);
    }

    // courbe de réponse
    Path curve, fill;
    const int steps = jmax (60, (int) p.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float x = p.getX() + p.getWidth() * (float) i / (float) steps;
        const float y = jlimit (p.getY(), p.getBottom(), dbToY (responseDb (xToF (x))));
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    fill = curve;
    fill.lineTo (p.getRight(), dbToY (0)); fill.lineTo (p.getX(), dbToY (0)); fill.closeSubPath();
    g.setColour (col::violet.withAlpha (0.16f * alpha));
    g.fillPath (fill);
    drawGlowPath (g, curve, col::violetHi, 1.5f, 0.6f * alpha);
    g.setColour (col::lilac.withAlpha (alpha));
    g.strokePath (curve, PathStrokeType (large ? 2.0f : 1.6f, PathStrokeType::curved, PathStrokeType::rounded));

    // points
    for (int i = 0; i < FxParams::kEqBands; ++i)
    {
        const auto c = col::layer (i);
        const auto pos = nodePos (i);
        const float h = nodeHover[(size_t) i];
        const float r = (large ? 7.0f : 5.0f) + h * 2.5f;
        if (h > 0.02f)
        {
            g.setColour (c.withAlpha (0.18f * h));
            g.fillEllipse (Rectangle<float> (r * 4, r * 4).withCentre (pos));
        }
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (Rectangle<float> (r * 2 + 3, r * 2 + 3).withCentre (pos));
        g.setColour (c.withAlpha (0.4f + 0.6f * alpha));
        g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (pos));
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (pos), 1.2f);
        if (large)
        {
            g.setFont (font (9.0f, true));
            g.setColour (Colours::black.withAlpha (0.8f));
            g.drawText (String (i + 1), Rectangle<float> (r * 2, r * 2).withCentre (pos), Justification::centred);
        }
    }

    // valeur du point actif
    const int a = dragging >= 0 ? dragging : hovered;
    if (a >= 0)
    {
        const auto& b = ctx.p.fx.eq[(size_t) a];
        String txt = String (FxParams::eqBandName (a)) + "   " + fmtFreq (b.freq) + "   "
                   + (b.gain >= 0 ? "+" : "") + String (b.gain, 1) + " dB   Q " + String (b.q, 2);
        g.setFont (font (large ? 12.0f : 9.5f, true));
        const float tw = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 16.0f;
        const float th = large ? 24.0f : 18.0f;
        auto pos = nodePos (a);
        auto box = Rectangle<float> (tw, th).withCentre ({ pos.x, pos.y - (large ? 26.0f : 18.0f) });
        if (box.getY() < p.getY()) box.setY (pos.y + (large ? 16.0f : 12.0f));
        box.setX (jlimit (p.getX(), p.getRight() - tw, box.getX()));
        g.setColour (col::bg2.withAlpha (0.95f));
        g.fillRoundedRectangle (box, th * 0.5f);
        g.setColour (col::layer (a).withAlpha (0.6f));
        g.drawRoundedRectangle (box, th * 0.5f, 1.0f);
        g.setColour (col::text);
        g.drawText (txt, box, Justification::centred);
    }
}

//==============================================================================
// FX
FxModule::FxModule (Ctx& c, int id, std::function<void()> ch) : ctx (c), fxId (id), changed (std::move (ch))
{
    sw.onToggle = [this] (bool on) { ctx.p.fx.on[(size_t) fxId] = on; changed(); };
    addAndMakeVisible (sw);
}

Knob& FxModule::addKnob (const String& label, double mn, double mx, double def,
                         std::function<float()> get, std::function<void (float)> set,
                         std::function<String (double)> fmt, double skewMid)
{
    auto k = std::make_unique<Knob> (label, mn, mx, def, col::violetHi);
    if (skewMid > 0) k->setSkewMid (skewMid);
    k->format = std::move (fmt);
    k->onChange = [this, set] (float v)
    {
        set (v);
        if (! ctx.p.fx.on[(size_t) fxId]) { ctx.p.fx.on[(size_t) fxId] = true; sw.setOn (true); } // toucher un knob allume le module
        changed();
    };
    addAndMakeVisible (*k);
    getters.push_back (std::move (get));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void FxModule::addSegment (StringArray items, std::function<int()> get, std::function<void (int)> set)
{
    seg = std::make_unique<Segmented> (items, false);
    segGet = std::move (get);
    seg->onChange = [this, set] (int i)
    {
        set (i);
        if (! ctx.p.fx.on[(size_t) fxId]) { ctx.p.fx.on[(size_t) fxId] = true; sw.setOn (true); }
        changed();
    };
    addAndMakeVisible (*seg);
}

void FxModule::switchOn()
{
    if (! ctx.p.fx.on[(size_t) fxId]) ctx.p.fx.on[(size_t) fxId] = true;
    sw.setOn (true);
}

void FxModule::refresh()
{
    sw.setOn (ctx.p.fx.on[(size_t) fxId]);
    for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->setValueQuiet (getters[i]());
    if (seg != nullptr) seg->setSelected (segGet());
}

int FxModule::preferredHeight() const
{
    if (custom != nullptr) return 30 + customH + 12;
    return 30 + (seg != nullptr ? 26 : 0) + 58 + 10;
}

void FxModule::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto top = r.removeFromTop (24);
    sw.setBounds (top.removeFromRight (36));
    if (hasExpand) { top.removeFromRight (4); expand.setBounds (top.removeFromRight (26)); }
    if (custom != nullptr) { r.removeFromTop (4); custom->setBounds (r.removeFromTop (customH)); return; }
    if (seg != nullptr) { r.removeFromTop (2); seg->setBounds (r.removeFromTop (22)); }
    r.removeFromTop (4);
    const int kw = jmin (54, r.getWidth() / jmax (1, (int) knobs.size()));
    auto row = r.withSizeKeepingCentre (kw * (int) knobs.size(), r.getHeight());
    for (auto& k : knobs) k->setBounds (row.removeFromLeft (kw));
}

void FxModule::tick (float dt)
{
    const float target = ctx.p.fx.on[(size_t) fxId] ? 1.0f : 0.38f;
    const float n = approach (dimShown, target, dt, 10.0f);
    if (std::abs (n - dimShown) > 0.003f)
    {
        dimShown = n;
        for (auto& k : knobs) { k->dim = dimShown; k->repaint(); }
        if (seg != nullptr) seg->setAlpha (dimShown);
        if (custom != nullptr) custom->repaint();
        repaint();
    }
}

void FxModule::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f, 3.0f);
    const float on = jmap (dimShown, 0.38f, 1.0f, 0.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (0.03f + 0.02f * on));
    g.fillRoundedRectangle (b, 14.0f);
    g.setColour (col::violet.withAlpha (0.08f + 0.3f * on));
    g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.0f);

    auto top = getLocalBounds().reduced (10, 6).removeFromTop (24).toFloat();
    auto dot = Rectangle<float> (6, 6).withCentre ({ top.getX() + 4, top.getCentreY() });
    g.setColour (col::violetHi.withAlpha (0.25f + 0.75f * on));
    g.fillEllipse (dot);
    if (on > 0.05f) { g.setColour (col::violetHi.withAlpha (0.25f * on)); g.fillEllipse (dot.expanded (3)); }
    g.setFont (fontSpaced (10.5f, true, 0.16f));
    g.setColour (col::text3.interpolatedWith (col::text, on));
    g.drawText (fxName (fxId), top.withTrimmedLeft (14), Justification::centredLeft);
}

FxPanel::FxPanel (Ctx& c, std::function<void()> ch)
    : ctx (c), changed (std::move (ch)),
      eq (c, [this] { if (eqModule != nullptr) eqModule->switchOn(); changed(); }, false)
{
    auto& fx = ctx.p.fx;
    auto mk = [this] (int id) { modules.push_back (std::make_unique<FxModule> (ctx, id, changed)); return modules.back().get(); };

    auto* m = mk (fxTransient);
    m->addKnob ("ATTACK", -1, 1, 0, [&fx] { return fx.trAttack; }, [&fx] (float v) { fx.trAttack = v; }, fmtBi).bipolar = true;
    m->addKnob ("SUSTAIN", -1, 1, 0, [&fx] { return fx.trSustain; }, [&fx] (float v) { fx.trSustain = v; }, fmtBi).bipolar = true;

    m = mk (fxEq);
    eqModule = m;
    m->setCustom (&eq, 112);
    m->hasExpand = true;
    m->expand.setTooltip (String::fromUTF8 ("Ouvrir l'EQ en grand"));
    m->expand.setWantsKeyboardFocus (false);
    m->expand.onClick = [this] { if (onExpandEq) onExpandEq(); };
    m->addAndMakeVisible (m->expand);
    eq.setTooltip (String::fromUTF8 ("Glisse les points · Shift = précis · molette = largeur · double-clic = reset"));

    m = mk (fxFilter);
    m->addKnob ("LOW CUT", 20, 2000, 20, [&fx] { return fx.hpFreq; }, [&fx] (float v) { fx.hpFreq = v; }, fmtHz, 200);
    m->addKnob ("HIGH CUT", 1000, 20000, 20000, [&fx] { return fx.lpFreq; }, [&fx] (float v) { fx.lpFreq = v; }, fmtHz, 6000);

    m = mk (fxDrive);
    m->addSegment ({ "SOFT", "HARD", "FOLD", "TUBE" }, [&fx] { return fx.driveType; }, [&fx] (int i) { fx.driveType = i; });
    m->addKnob ("DRIVE", 0, 1, 0.3, [&fx] { return fx.drive; }, [&fx] (float v) { fx.drive = v; }, fmtPct);
    m->addKnob ("MIX", 0, 1, 1, [&fx] { return fx.driveMix; }, [&fx] (float v) { fx.driveMix = v; }, fmtPct);

    m = mk (fxCrush);
    m->addKnob ("BITS", 3, 16, 12, [&fx] { return fx.crushBits; }, [&fx] (float v) { fx.crushBits = v; }, [] (double v) { return String (v, 1) + " bit"; });
    m->addKnob ("RATE", 1, 24, 1, [&fx] { return fx.crushRate; }, [&fx] (float v) { fx.crushRate = v; }, [] (double v) { return String::fromUTF8 ("÷") + String (roundToInt (v)); });
    m->addKnob ("MIX", 0, 1, 1, [&fx] { return fx.crushMix; }, [&fx] (float v) { fx.crushMix = v; }, fmtPct);

    m = mk (fxComp);
    m->addKnob ("THRESH", -40, 0, -18, [&fx] { return fx.compThresh; }, [&fx] (float v) { fx.compThresh = v; }, fmtDb);
    m->addKnob ("RATIO", 1, 20, 4, [&fx] { return fx.compRatio; }, [&fx] (float v) { fx.compRatio = v; }, [] (double v) { return String (v, 1) + ":1"; }, 4);

    m = mk (fxWidth);
    m->addKnob ("WIDTH", 0, 2, 1, [&fx] { return fx.width; }, [&fx] (float v) { fx.width = v; }, fmtPct);

    m = mk (fxReverb);
    m->addKnob ("SIZE", 0, 1, 0.4, [&fx] { return fx.revSize; }, [&fx] (float v) { fx.revSize = v; }, fmtPct);
    m->addKnob ("MIX", 0, 0.6, 0.15, [&fx] { return fx.revMix; }, [&fx] (float v) { fx.revMix = v; }, fmtPct);

    for (auto& mod : modules) list.addAndMakeVisible (*mod);
    view.setViewedComponent (&list, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (6);
    addAndMakeVisible (view);

    enrich.setWantsKeyboardFocus (false);
    reset.setWantsKeyboardFocus (false);
    enrich.textSize = 14.0f;
    enrich.onClick = [this]
    {
        Random r;
        ctx.p.fx.randomise (r);
        refresh();
        changed();
        StringArray names;
        for (int i = 0; i < kNumFx; ++i) if (ctx.p.fx.on[(size_t) i]) names.add (fxName (i));
        ctx.toast.show ("Enrichi : " + names.joinIntoString (String::fromUTF8 (" · ")));
        ctx.burstAt (enrich, enrich.getLocalBounds().getCentre().toFloat(), 48);
    };
    reset.onClick = [this]
    {
        const auto keepNorm = ctx.p.fx.normalize;
        ctx.p.fx.reset();
        ctx.p.fx.normalize = keepNorm;
        refresh();
        changed();
        ctx.toast.show (String::fromUTF8 ("Effets remis à zéro"), col::lilac);
    };
    addAndMakeVisible (enrich);
    addAndMakeVisible (reset);

    out.format = fmtDb;
    out.onChange = [this] (float v) { ctx.p.fx.outGainDb = v; changed(); };
    addAndMakeVisible (out);
    normalize.onToggle = [this] (bool b) { ctx.p.fx.normalize = b; changed(); };
    addAndMakeVisible (normalize);
    refresh();
}

void FxPanel::refresh()
{
    for (auto& m : modules) m->refresh();
    eq.repaint();
    out.setValueQuiet (ctx.p.fx.outGainDb);
    normalize.setOn (ctx.p.fx.normalize);
}

void FxPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (54);
    reset.setBounds (top.removeFromRight (64).withSizeKeepingCentre (64, 34));
    enrich.setBounds (top.reduced (0, 1));
    r.removeFromTop (10);
    auto bottom = r.removeFromBottom (70);
    view.setBounds (r);

    int y = 0;
    const int w = r.getWidth() - 8;
    for (auto& m : modules)
    {
        const int h = m->preferredHeight();
        m->setBounds (0, y, w, h);
        y += h + 2;
    }
    list.setSize (w, y);

    bottom.removeFromTop (8);
    auto ob = bottom.reduced (12, 4);
    out.setBounds (ob.removeFromLeft (54));
    normalize.setBounds (ob.removeFromRight (40).withSizeKeepingCentre (36, 24).translated (0, -6));
}

void FxPanel::paint (Graphics& g)
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (62).toFloat();
    drawCard (g, bottom, 16.0f);
    g.setFont (fontSpaced (10.0f, true, 0.16f));
    g.setColour (col::text3);
    g.drawText ("NORMALIZE", bottom.reduced (12, 0).withTrimmedLeft (60).withTrimmedRight (40).withTrimmedBottom (12), Justification::centredRight);
    g.setFont (font (9.5f));
    g.drawText ("-0.3 dB peak", bottom.reduced (12, 0).withTrimmedLeft (60).withTrimmedRight (40).withTrimmedTop (22), Justification::centredRight);
}

//==============================================================================
// EQ en grand
EqOverlay::EqOverlay (Ctx& c, std::function<void()> ch) : ctx (c), graph (c, [this, ch] { power.setOn (ctx.p.fx.on[fxEq]); ch(); }, true)
{
    addAndMakeVisible (graph);
    closeBtn.onClick = [this] { close(); };
    resetBtn.onClick = [this, ch]
    {
        ctx.p.fx.eq = FxParams().eq;
        graph.repaint();
        ch();
    };
    power.onToggle = [this, ch] (bool b) { ctx.p.fx.on[fxEq] = b; graph.repaint(); ch(); };
    for (auto* b : { (Button*) &closeBtn, (Button*) &resetBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (b); }
    addAndMakeVisible (power);
    setVisible (false);
}

void EqOverlay::open()
{
    power.setOn (ctx.p.fx.on[fxEq]);
    target = 1.0f;
    setVisible (true);
    toFront (false);
}

void EqOverlay::close() { target = 0.0f; }

void EqOverlay::resized()
{
    card = getLocalBounds();
    auto r = card.reduced (22, 18);
    auto head = r.removeFromTop (34);
    closeBtn.setBounds (head.removeFromRight (34));
    head.removeFromRight (8);
    resetBtn.setBounds (head.removeFromRight (110).withSizeKeepingCentre (110, 32));
    head.removeFromRight (10);
    power.setBounds (head.removeFromRight (40).withSizeKeepingCentre (36, 22));
    r.removeFromTop (10);
    r.removeFromBottom (26);
    graph.setBounds (r);
}

void EqOverlay::tick (float dt)
{
    if (! isVisible()) return;
    const float n = approach (shown, target, dt, 16.0f);
    if (std::abs (n - shown) > 0.001f || (target < 0.5f && shown > 0.0f))
    {
        shown = n;
        setAlpha (jlimit (0.0f, 1.0f, shown));
        const float sc = 0.96f + 0.04f * shown;
        const auto c = getLocalBounds().toFloat().getCentre();
        setTransform (AffineTransform::scale (sc, sc, c.x, c.y).translated (0, (1.0f - shown) * 10.0f));
        if (target < 0.5f && shown < 0.01f) { shown = 0; setVisible (false); setTransform ({}); }
    }
}

void EqOverlay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::bg1);
    g.fillRoundedRectangle (b, 22.0f);
    drawCard (g, b, 22.0f, 0.4f);
    auto r = card.reduced (22, 18);
    auto head = r.removeFromTop (34);
    g.setFont (fontSpaced (18.0f, true, 0.06f));
    g.setColour (col::text);
    g.drawText ("EQ", head.removeFromLeft (44), Justification::centredLeft);
    g.setFont (font (11.0f));
    g.setColour (col::text3);
    g.drawText (String::fromUTF8 ("4 points à la main · précision au Hz"), head, Justification::centredLeft);
    auto foot = r.removeFromBottom (22);
    g.setFont (fontSpaced (9.5f, true, 0.14f));
    g.drawText (String::fromUTF8 ("GLISSE  ·  SHIFT = PRÉCIS  ·  ALT = FRÉQ SEULE  ·  CMD = GAIN SEUL  ·  MOLETTE = LARGEUR  ·  DOUBLE-CLIC = RESET"),
                foot, Justification::centred);
}

void EqOverlay::mouseUp (const MouseEvent&) {}

//==============================================================================
// MixPage
MixPage::MixPage (Ctx& c) : ctx (c), result (&c.p), timing (c, [this] { changed(); }), fx (c, [this] { changed(); }),
                             eqBig (c, [this] { fx.refresh(); changed(); })
{
    fx.onExpandEq = [this] { eqBig.open(); };
    for (int i = 0; i < kNumLayers; ++i)
    {
        cards.push_back (std::make_unique<LayerCard> (ctx, i, [this] { changed(); }));
        addAndMakeVisible (*cards.back());
    }
    result.colour = col::violetHi;
    result.allowDragOut = true;
    result.dragName = "K1T kit";
    result.emptyText = String::fromUTF8 ("Ton kit apparaîtra ici");
    result.onClick = [this] { playKit(); };
    addAndMakeVisible (result);

    playBtn.onClick = [this] { playKit(); };
    spread.format = [] (double v) { return String (v, 1) + " ms"; };
    spread.onChange = [this] (float v) { ctx.p.spreadMs = v; changed(); };
    presets.onChange = [this] (int i)
    {
        static const float table[3][4] = { { 0, 0, 0, 0 }, { 0, 1.6f, -0.9f, 3.2f }, { 0, 6.0f, 12.0f, 18.0f } };
        int n = 0;
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& L = ctx.p.layers[(size_t) l];
            if (L.src == nullptr) continue;
            L.nudgeMs = table[i][n++];
        }
        ctx.p.spreadMs = i == 1 ? 0.6f : 0.0f;
        refresh();
        changed();
    };
    shuffle.onClick = [this]
    {
        ctx.p.spreadSeed = (uint32) Random::getSystemRandom().nextInt (0x7fffffff) + 1;
        if (ctx.p.spreadMs < 0.5f) ctx.p.spreadMs = 3.0f;
        refresh();
        changed();
        ctx.burstAt (shuffle, shuffle.getLocalBounds().getCentre().toFloat(), 18);
        ctx.toast.show (String::fromUTF8 ("Nouveau groove : décalages ±") + String (ctx.p.spreadMs, 1) + " ms");
    };

    dragOut.onDragOut = [this] (Component* src)
    {
        if (auto k = ctx.p.getKit()) dragSampleOut (ctx.p, *k, result.dragName, src);
    };
    dragOut.onClick = [this] { ctx.toast.show ("Maintiens et glisse le bouton vers FL Studio ou le Finder"); };
    save.onClick = [this]
    {
        auto k = ctx.p.getKit();
        if (k == nullptr) { ctx.toast.show ("Ajoute d'abord un sample", col::pink); return; }
        chooser = std::make_unique<FileChooser> ("Enregistrer le kit", K1TProcessor::appFolder().getChildFile (safeFileName (result.dragName) + ".wav"), "*.wav");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this, k] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File()) return;
                                  if (writeWav (*k, f.withFileExtension ("wav"))) ctx.toast.show (String::fromUTF8 ("Enregistré : ") + f.getFileName(), col::green);
                                  else ctx.toast.show (String::fromUTF8 ("Impossible d'écrire le fichier"), col::red);
                              });
    };
    toKit.onClick = [this]
    {
        auto k = ctx.p.getKit();
        if (k == nullptr) { ctx.toast.show ("Ajoute d'abord un sample", col::pink); return; }
        String hint;
        for (auto& L : ctx.p.layers) if (L.src != nullptr) { hint = L.src->name; break; }
        auto base = result.dragName.upToLastOccurrenceOf (" (K1T)", false, false);
        auto f = ctx.p.addToKit (*k, base + " K1T", hint);
        if (f == File()) { ctx.toast.show (String::fromUTF8 ("Impossible d'écrire dans le kit"), col::red); return; }
        ctx.burstAt (toKit, toKit.getLocalBounds().getCentre().toFloat(), 30);
        ctx.toast.show (String::fromUTF8 ("Ajouté au kit  →  ") + f.getParentDirectory().getFileName() + String::fromUTF8 ("  ·  ") + String (ctx.p.kitCount) + " sons", col::green);
        toKit.setButtonText ("KIT  " + String (ctx.p.kitCount));
    };
    openKit.onClick = [this] { ctx.p.kitFolder().revealToUser(); };
    openKit.setTooltip ("Ouvrir le dossier du kit");
    if (ctx.p.kitCount > 0) toKit.setButtonText ("KIT  " + String (ctx.p.kitCount));
    choke.onToggle = [this] (bool b) { ctx.p.choke = b; changed(); };
    choke.setTooltip (String::fromUTF8 ("CHOKE : quand une nouvelle note part, elle coupe la précédente "
                                        "(les sons ne se chevauchent plus — idéal pour les hats et les 808)."));

    for (auto* b : { (Button*) &playBtn, (Button*) &shuffle, (Button*) &dragOut, (Button*) &save, (Button*) &toKit, (Button*) &openKit }) b->setWantsKeyboardFocus (false);
    dragOut.textSize = 13.0f;
    for (Component* comp : std::initializer_list<Component*> { &playBtn, &spread, &presets, &shuffle, &timing, &dragOut, &save, &toKit, &openKit, &choke, &fx })
        addAndMakeVisible (comp);
    addChildComponent (eqBig);
    refresh();
}

void MixPage::refresh()
{
    for (auto& c : cards) c->refresh();
    spread.setValueQuiet (ctx.p.spreadMs);
    choke.setOn (ctx.p.choke);
    fx.refresh();
    timing.repaint();
}

void MixPage::kitChanged()
{
    auto k = ctx.p.getKit();
    result.setSample (k, result.getSample() == nullptr);
    fx.kitChanged (k.get());
    eqBig.setSpectrum (k.get());
    String n;
    for (auto& L : ctx.p.layers) if (L.active()) { n = L.src->name; break; }
    result.dragName = n.isNotEmpty() ? n + " (K1T)" : "K1T kit";
    repaint (resultCard);
}

void MixPage::changed()
{
    ctx.p.requestRender();
    timing.repaint();
    fx.repaintEq();
    eqBig.repaintGraph();
}

bool MixPage::goBack()
{
    if (! ctx.p.undoMix()) { ctx.toast.show (String::fromUTF8 ("Rien avant ça"), col::lilac); return false; }
    refresh();
    kitChanged();
    ctx.toast.show (String::fromUTF8 ("↶  Retour en arrière"), col::lilac);
    return true;
}

void MixPage::playKit()
{
    if (auto k = ctx.p.getKit()) ctx.p.preview (k);
}

void MixPage::addFiles (const StringArray& files, int start)
{
    int idx = start;
    int loaded = 0;
    for (auto& path : files)
    {
        if (! isAudioFile (path)) continue;
        if (idx < 0 || idx >= kNumLayers) break;
        auto err = ctx.p.loadLayer (idx, File (path));
        if (err.isNotEmpty()) { ctx.toast.show (err, col::red); continue; }
        ++loaded;
        int next = -1;
        for (int i = idx + 1; i < kNumLayers; ++i) if (ctx.p.layers[(size_t) i].src == nullptr) { next = i; break; }
        if (next < 0) for (int i = 0; i < kNumLayers; ++i) if (ctx.p.layers[(size_t) i].src == nullptr) { next = i; break; }
        idx = next;
    }
    refresh();
    changed();
    if (loaded > 1) ctx.toast.show (String (loaded) + String::fromUTF8 (" sons mélangés ensemble"));
}

void MixPage::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (360);
    r.removeFromLeft (16);
    auto right = r.removeFromRight (240);
    r.removeFromRight (16);
    auto centre = r;

    layersArea = left;
    left.removeFromTop (26);
    const int ch = (left.getHeight() - 3 * 10) / 4;
    for (auto& c : cards) { c->setBounds (left.removeFromTop (ch)); left.removeFromTop (10); }

    fx.setBounds (right);
    eqBig.setBounds (getLocalBounds().withRight (right.getX() - 16));

    resultCard = centre.removeFromTop (306);
    auto rc = resultCard.reduced (16, 12);
    rc.removeFromTop (24);
    result.setBounds (rc.removeFromTop (150));
    rc.removeFromTop (10);
    auto row = rc.removeFromTop (60);
    playBtn.setBounds (row.removeFromLeft (100).withSizeKeepingCentre (100, 38));
    row.removeFromLeft (6);
    spread.setBounds (row.removeFromLeft (58));
    shuffle.setBounds (row.removeFromRight (40).withSizeKeepingCentre (40, 38));
    row.removeFromRight (6);
    presets.setBounds (row.withSizeKeepingCentre (row.getWidth(), 26));

    centre.removeFromTop (10);
    timingCard = centre.removeFromTop (156);
    timing.setBounds (timingCard);
    centre.removeFromTop (10);
    exportRow = centre;
    auto er = exportRow.reduced (16, 14);
    dragOut.setBounds (er.removeFromTop (50));
    er.removeFromTop (8);
    auto er2 = er.removeFromTop (40);
    save.setBounds (er2.removeFromLeft (122));
    er2.removeFromLeft (8);
    toKit.setBounds (er2.removeFromLeft (92));
    er2.removeFromLeft (4);
    openKit.setBounds (er2.removeFromLeft (40));
    choke.setBounds (er2.removeFromRight (40).withSizeKeepingCentre (36, 24));
}

void MixPage::paint (Graphics& g)
{
    sectionTitle (g, layersArea.withHeight (20), "LAYERS");
    g.setFont (font (10.5f));
    g.setColour (col::text3);
    g.drawText (String::fromUTF8 ("drop jusqu'à 4 sons  ·  ils se mélangent"), layersArea.withHeight (20), Justification::centredRight);

    drawCard (g, resultCard.toFloat(), 20.0f);
    auto t = resultCard.reduced (16, 12).removeFromTop (20);
    sectionTitle (g, t, String::fromUTF8 ("RÉSULTAT"));
    if (auto k = ctx.p.getKit())
    {
        g.setFont (font (10.5f));
        g.setColour (col::text3);
        g.drawText (String (roundToInt (k->seconds() * 1000.0)) + String::fromUTF8 (" ms  ·  44.1 kHz  ·  24-bit"), t, Justification::centredRight);
    }

    drawCard (g, exportRow.toFloat(), 20.0f);
    auto er = exportRow.reduced (16, 14);
    er.removeFromTop (58);
    auto er2 = er.removeFromTop (40);
    g.setFont (fontSpaced (10.0f, true, 0.16f));
    g.setColour (col::text3);
    g.drawText ("CHOKE", er2.withTrimmedRight (44), Justification::centredRight);
}

//==============================================================================
// VariantCard
VariantCard::VariantCard (Ctx& c, int i) : ctx (c), index (i), wave (&c.p)
{
    wave.compact = true;
    wave.setInterceptsMouseClicks (false, false);
    wave.colour = i % 2 == 0 ? col::violetHi : col::magenta;
    addAndMakeVisible (wave);
    add.setWantsKeyboardFocus (false);
    add.textSize = 10.5f;
    add.onClick = [this] { if (sample != nullptr && onAddLayer) onAddLayer (sample); };
    addAndMakeVisible (add);
    kit.onClick = [this] { if (sample != nullptr && onAddKit) onAddKit (sample); };
    addAndMakeVisible (kit);
    exp.onClick = [this] { if (sample != nullptr && onExport) onExport (sample); };
    exp.setTooltip ("Exporter ce son en WAV");
    exp.setWantsKeyboardFocus (false);
    addAndMakeVisible (exp);
    add.setWantsKeyboardFocus (false);
    kit.setWantsKeyboardFocus (false);
    setRepaintsOnMouseActivity (true);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void VariantCard::setSample (SamplePtr s, float d)
{
    sample = std::move (s);
    wave.setSample (sample, true);
    appear = 0.0f;
    delay = d;
    add.setVisible (sample != nullptr);
    setAlpha (0.0f);
}

void VariantCard::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (26);
    auto bottom = r.removeFromBottom (30);
    exp.setBounds (bottom.removeFromRight (28));
    add.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2));
    kit.setBounds (bottom);
    r.removeFromBottom (6);
    wave.setBounds (r);
}

void VariantCard::tick (float dt)
{
    bool rp = false;
    if (delay > 0) delay -= dt;
    else if (appear < 1.0f)
    {
        appear = jmin (1.0f, appear + dt / 0.42f);
        const float e = 1.0f - std::pow (1.0f - appear, 3.0f);
        const float back = 1.0f + 0.08f * std::sin (appear * MathConstants<float>::pi);
        setAlpha (e);
        const float sc = (0.86f + 0.14f * e) * (appear < 1.0f ? back : 1.0f);
        const auto bc = getBounds().toFloat().getCentre();
        setTransform (AffineTransform::scale (sc, sc, bc.x, bc.y).translated (0, (1.0f - e) * 14.0f));
        if (appear >= 1.0f) setTransform ({});
    }
    const float nh = approach (hover, isMouseOver (true) ? 1.0f : 0.0f, dt, 12.0f);
    const bool isPlaying = sample != nullptr && ctx.p.playingSample() == sample.get() && ctx.p.playingPosition() >= 0;
    const float np = approach (playing, isPlaying ? 1.0f : 0.0f, dt, 10.0f);
    if (std::abs (nh - hover) > 0.002f || std::abs (np - playing) > 0.002f) rp = true;
    hover = nh; playing = np;
    if (rp) repaint();
}

void VariantCard::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    if (sample == nullptr)
    {
        dashedRect (g, b, 18.0f, 0, Colours::white.withAlpha (0.08f));
        return;
    }
    const auto c = wave.colour;
    if (playing > 0.01f)
    {
        Path p; p.addRoundedRectangle (b, 18.0f);
        drawGlowPath (g, p, c, 1.0f, 0.4f * playing);
    }
    drawCard (g, b, 18.0f, jmax (hover, playing), c);
    auto top = getLocalBounds().reduced (14, 12).removeFromTop (22);
    g.setFont (fontSpaced (10.0f, true, 0.14f));
    g.setColour (c);
    g.drawText ("V" + String (index + 1).paddedLeft ('0', 2), top, Justification::centredLeft);
    g.setFont (font (10.5f));
    g.setColour (col::text3);
    g.drawText (String (roundToInt (sample->seconds() * 1000.0)) + " ms", top, Justification::centredRight);
}

void VariantCard::mouseUp (const MouseEvent& e)
{
    if (! dragged && e.mouseWasClicked() && sample != nullptr) ctx.p.preview (sample);
    dragged = false;
}

void VariantCard::mouseDrag (const MouseEvent& e)
{
    if (dragged || sample == nullptr || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    dragSampleOut (ctx.p, *sample, sample->name, this);
}

//==============================================================================
// GenPage
GenPage::GenPage (Ctx& c) : ctx (c), ref (&c.p)
{
    ref.colour = col::violetHi;
    ref.emptyText = "";
    ref.onClick = [this] { if (ctx.p.reference != nullptr) playReference(); else { chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Sample de référence"), File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) loadRef (fc.getResult()); }); } };
    addAndMakeVisible (ref);

    amount.format = fmtPct;
    amount.onChange = [this] (float v) { ctx.p.genAmount = v; };
    addAndMakeVisible (amount);

    mode.onChange = [this] (int m)
    {
        ctx.p.genMode = m;
        repaint();
        ctx.toast.show (m == 1 ? String::fromUTF8 ("Mode 808 : toujours accordées en C — longueur, saturation, punch")
                               : String::fromUTF8 ("Mode TOUT : hats, claps, snares, percs, kicks…"), col::lilac);
        if (ctx.p.reference != nullptr && ! ctx.p.variants.empty()) runGenerate();
    };
    addAndMakeVisible (mode);

    generate.textSize = 15.0f;
    generate.onClick = [this] { runGenerate(); };
    addAndMakeVisible (generate);

    refPlay.onClick = [this] { playReference(); };
    addAndMakeVisible (refPlay);

    exportAll.onClick = [this]
    {
        if (ctx.p.variants.empty()) return;
        chooser = std::make_unique<FileChooser> ("Dossier d'export", K1TProcessor::appFolder());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
        {
            auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            int n = 0;
            for (auto& v : ctx.p.variants) if (writeWav (*v, dir.getChildFile (safeFileName (v->name) + ".wav"))) ++n;
            ctx.toast.show (String (n) + String::fromUTF8 (" variantes exportées"), col::green);
            dir.revealToUser();
        });
    };
    addAndMakeVisible (exportAll);

    for (int i = 0; i < 8; ++i)
    {
        cards.push_back (std::make_unique<VariantCard> (ctx, i));
        cards.back()->onAddLayer = [this] (SamplePtr s)
        {
            int idx = ctx.p.firstEmptyLayer();
            if (idx < 0) idx = kNumLayers - 1;
            const auto f = K1TProcessor::appFolder().getChildFile ("Generated").getChildFile (safeFileName (s->name) + ".wav");
            writeWav (*s, f);
            ctx.p.setLayer (idx, s, f.getFullPathName());
            if (onLayersChanged) onLayersChanged();
            ctx.toast.show (s->name + String::fromUTF8 ("  →  layer ") + String (idx + 1));
        };
        cards.back()->onExport = [this] (SamplePtr s) { exportOne (s); };
        cards.back()->onAddKit = [this] (SamplePtr s)
        {
            auto f = ctx.p.addToKit (*s, s->name, ctx.p.reference != nullptr ? ctx.p.reference->name : String());
            if (f == File()) { ctx.toast.show (String::fromUTF8 ("Impossible d'écrire dans le kit"), col::red); return; }
            ctx.toast.show (s->name + String::fromUTF8 ("  →  kit / ") + f.getParentDirectory().getFileName()
                            + String::fromUTF8 ("  ·  ") + String (ctx.p.kitCount) + " sons", col::green);
        };
        addAndMakeVisible (*cards.back());
    }
    for (auto* b : { &generate, &exportAll, &refPlay }) b->setWantsKeyboardFocus (false);
    refresh (false);
}

void GenPage::refresh (bool animate)
{
    ref.setSample (ctx.p.reference, animate);
    amount.setValueQuiet (ctx.p.genAmount);
    mode.setSelected (ctx.p.genMode);
    refPlay.setVisible (ctx.p.reference != nullptr);
    generate.setEnabled (ctx.p.reference != nullptr);
    exportAll.setEnabled (! ctx.p.variants.empty());
    for (int i = 0; i < 8; ++i)
    {
        auto s = i < (int) ctx.p.variants.size() ? ctx.p.variants[(size_t) i] : nullptr;
        cards[(size_t) i]->setSample (s, animate ? 0.05f * (float) i : 0.0f);
    }
    repaint();
}

void GenPage::loadRef (const File& f)
{
    auto err = ctx.p.loadReference (f);
    if (err.isNotEmpty()) { ctx.toast.show (err, col::red); return; }
    const auto n = f.getFileNameWithoutExtension().toLowerCase();
    const bool other = n.contains ("hat") || n.contains ("hh") || n.contains ("clap") || n.contains ("snare")
                    || n.contains ("perc") || n.contains ("kick") || n.contains ("rim") || n.contains ("cowbell");
    if (! other && (n.contains ("808") || n.contains ("bass") || n.contains ("sub")))
    {
        ctx.p.genMode = 1;
        ctx.toast.show (String::fromUTF8 ("808 détectée → mode 808"), col::lilac);
    }
    refresh (true);
    ctx.p.preview (ctx.p.reference);
}

void GenPage::runGenerate()
{
    if (ctx.p.reference == nullptr) { ctx.toast.show (String::fromUTF8 ("Drop d'abord un son de référence"), col::pink); return; }
    ctx.p.generate (true);
    refresh (true);
    ctx.burstAt (generate, generate.getLocalBounds().getCentre().toFloat(), 44);
    if (ctx.p.genMode == 1)
    {
        const auto f0 = detect808Pitch (*ctx.p.reference);
        ctx.toast.show (f0 > 0 ? String::fromUTF8 ("808 d'origine en ") + noteName (f0) + String::fromUTF8 ("  →  8 nouvelles 808 accordées en C")
                               : String::fromUTF8 ("8 nouvelles 808 (note non détectée, pitch d'origine gardé)"));
    }
    else
        ctx.toast.show (String::fromUTF8 ("8 nouveaux sons inventés depuis ") + ctx.p.reference->name);
}

void GenPage::playReference() { if (ctx.p.reference != nullptr) ctx.p.preview (ctx.p.reference); }

void GenPage::exportOne (SamplePtr s)
{
    const auto dir = K1TProcessor::appFolder().getChildFile ("Exports");
    dir.createDirectory();
    chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Exporter ") + s->name, dir.getChildFile (safeFileName (s->name) + ".wav"), "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this, s] (const FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == File()) return;
                              if (writeWav (*s, f.withFileExtension ("wav"))) ctx.toast.show (String::fromUTF8 ("Exporté : ") + f.withFileExtension ("wav").getFileName(), col::green);
                              else ctx.toast.show (String::fromUTF8 ("Impossible d'écrire le fichier"), col::red);
                          });
}

bool GenPage::goBack()
{
    if (! ctx.p.undoGenerate()) { ctx.toast.show (String::fromUTF8 ("Rien avant ça"), col::lilac); return false; }
    refresh (true);
    ctx.toast.show (String::fromUTF8 ("↶  Génération précédente"), col::lilac);
    return true;
}

void GenPage::resized()
{
    auto r = getLocalBounds();
    refCard = r.removeFromLeft (360);
    r.removeFromLeft (16);
    gridArea = r;

    auto rc = refCard.reduced (18, 16);
    rc.removeFromTop (28);
    refWaveArea = rc.removeFromTop (170);
    ref.setBounds (refWaveArea.reduced (10, 16));
    auto nameRow = rc.removeFromTop (34);
    refPlay.setBounds (nameRow.removeFromRight (34));
    rc.removeFromTop (10);
    mode.setBounds (rc.removeFromTop (30).withSizeKeepingCentre (200, 30));
    rc.removeFromTop (12);
    amount.setBounds (rc.removeFromTop (100).withSizeKeepingCentre (92, 100));
    rc.removeFromTop (16);
    generate.setBounds (rc.removeFromTop (56));

    auto g = gridArea;
    auto head = g.removeFromTop (40);
    exportAll.setBounds (head.removeFromRight (150).withSizeKeepingCentre (150, 34));
    g.removeFromTop (6);
    const int gap = 12;
    const int cw = (g.getWidth() - 3 * gap) / 4, chh = (g.getHeight() - gap) / 2;
    for (int i = 0; i < 8; ++i)
    {
        const int col = i % 4, row = i / 4;
        cards[(size_t) i]->setTransform ({});
        cards[(size_t) i]->setBounds (g.getX() + col * (cw + gap), g.getY() + row * (chh + gap), cw, chh);
    }
}

void GenPage::tick (float dt)
{
    const float nd = approach (drop, dragOver ? 1.0f : 0.0f, dt, 14.0f);
    const bool anim = ctx.p.reference == nullptr || drop > 0.01f;
    if (anim) dash += dt * 14.0f;
    if (std::abs (nd - drop) > 0.002f || anim) { drop = nd; repaint (refWaveArea.expanded (4)); }
}

void GenPage::paint (Graphics& g)
{
    drawCard (g, refCard.toFloat(), 22.0f, drop);
    auto rc = refCard.reduced (18, 16);
    sectionTitle (g, rc.removeFromTop (20), String::fromUTF8 ("RÉFÉRENCE"));

    auto wa = refWaveArea.toFloat();
    if (ctx.p.reference == nullptr)
    {
        g.setColour (Colours::white.withAlpha (0.02f + 0.05f * drop));
        g.fillRoundedRectangle (wa, 16.0f);
        dashedRect (g, wa.reduced (1), 16.0f, dash, col::violetHi.withAlpha (0.3f + 0.5f * drop));
        const float bob = std::sin (AnimHub::globalTime * 2.2f) * 3.0f;
        auto ic = Rectangle<float> (40, 40).withCentre (wa.getCentre().translated (0, -16 + bob));
        g.setColour (col::violetHi.withAlpha (0.8f));
        g.fillPath (icon::sparkle (ic));
        g.setFont (font (13.0f, true));
        g.setColour (col::text);
        g.drawText (String::fromUTF8 ("Drop un hat, un clap, un kick…"), wa.withTrimmedTop (wa.getHeight() * 0.58f).withHeight (18), Justification::centred);
        g.setFont (font (11.0f));
        g.setColour (col::text3);
        g.drawText ("ou clique pour choisir", wa.withTrimmedTop (wa.getHeight() * 0.58f + 20).withHeight (16), Justification::centred);
    }
    else
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (wa, 16.0f);
        auto nameRow = refWaveArea.withY (refWaveArea.getBottom()).withHeight (34);
        g.setFont (font (14.0f, true));
        g.setColour (col::text);
        g.drawText (ctx.p.reference->name, nameRow.withTrimmedRight (40), Justification::centredLeft, true);
    }

    auto hint = refCard.reduced (22, 18).removeFromBottom (62);
    g.setFont (font (11.0f));
    g.setColour (col::text3);
    g.drawFittedText (ctx.p.genMode == 1
                        ? String::fromUTF8 ("Mode 808 : toutes les 808 sortent accordées en C (C5 dans FL), sans autre pitch. K1T varie seulement la longueur, la saturation, le punch et le sub.")
                        : String::fromUTF8 ("K1T garde l'ADN de ton son (timbre, attaque) et en invente 8 nouvelles prises : pitch, longueur, couleur, texture. Clique pour écouter, glisse vers FL, ou envoie en layer."),
                      hint, Justification::centredLeft, 4);

    auto ga = gridArea;
    auto head = ga.removeFromTop (40);
    sectionTitle (g, head, "VARIANTES");
    if (ctx.p.variants.empty())
    {
        g.setFont (font (12.0f));
        g.setColour (col::text3);
        g.drawText ("Appuie sur GENERATE pour inventer de nouveaux sons", ga, Justification::centred);
    }

    // repères du knob VARIATION
    {
        auto kb = amount.getBounds();
        g.setFont (fontSpaced (9.0f, true, 0.12f));
        g.setColour (col::text3);
        g.drawText ("SUBTIL", kb.withX (kb.getX() - 70).withWidth (62), Justification::centredRight);
        g.drawText ("RADICAL", kb.withX (kb.getRight() + 8).withWidth (70), Justification::centredLeft);
    }
}

void GenPage::mouseUp (const MouseEvent& e)
{
    if (refWaveArea.contains (e.getPosition()) && ctx.p.reference == nullptr && ref.onClick) ref.onClick();
}

bool GenPage::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}

void GenPage::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files) if (isAudioFile (f)) { loadRef (File (f)); break; }
}

//==============================================================================
// FlpPage
FlpPage::FlpPage (Ctx& c) : ctx (c)
{
    for (auto* lb : { &midiList, &smpList })
    {
        lb->setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        lb->setColour (ListBox::outlineColourId, Colours::transparentBlack);
        lb->setRowHeight (44);
        addChildComponent (*lb);
    }
    midiList.setModel (&midiModel);
    smpList.setModel (&smpModel);

    extractBtn.textSize = 13.0f;
    extractBtn.onClick = [this] { extract(); };
    searchBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Dossier où chercher tes samples"), ctx.p.flpSearchDir);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
        {
            auto dir = fc.getResult();
            if (! dir.isDirectory() || ctx.p.flp == nullptr) return;
            ctx.p.flpSearchDir = dir;
            const int before = ctx.p.flp->numSamplesFound();
            resolveSamples (*ctx.p.flp, dir);
            renders.clear();
            for (auto& [id, ch] : ctx.p.flp->channels) if (ch.resolved.existsAsFile()) selChannels.insert (id);
            refresh();
            ctx.toast.show (String (ctx.p.flp->numSamplesFound() - before) + String::fromUTF8 (" samples retrouvés en plus"), col::green);
        });
    };
    otherBtn.onClick = [this] { browse(); };

    allPat.onClick = [this]
    {
        const bool all = (int) selPatterns.size() == (int) patRows.size();
        selPatterns.clear();
        if (! all) for (auto* p : patRows) selPatterns.insert (p->id);
        midiList.repaint(); updateExtractLabel();
    };
    allSmp.onClick = [this]
    {
        const bool all = (int) selChannels.size() == (int) smpRows.size();
        selChannels.clear();
        if (! all) for (auto* c : smpRows) selChannels.insert (c->id);
        smpList.repaint(); updateExtractLabel();
    };
    for (auto* b : { &extractBtn, &searchBtn, &otherBtn, &allPat, &allSmp }) { b->setWantsKeyboardFocus (false); addChildComponent (*b); }
    allPat.textSize = allSmp.textSize = 10.0f;
    setMouseCursor (MouseCursor::NormalCursor);
    refresh();
}

int FlpPage::Model::getNumRows() { return samples ? (int) page.smpRows.size() : (int) page.patRows.size(); }

static void drawCheck (Graphics& g, Rectangle<float> b, bool on, Colour c)
{
    g.setColour (on ? c : Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (on ? c.brighter (0.2f) : Colours::white.withAlpha (0.25f));
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
    if (on)
    {
        Path p;
        p.startNewSubPath (b.getX() + b.getWidth() * 0.25f, b.getCentreY());
        p.lineTo (b.getX() + b.getWidth() * 0.43f, b.getBottom() - b.getHeight() * 0.28f);
        p.lineTo (b.getRight() - b.getWidth() * 0.22f, b.getY() + b.getHeight() * 0.28f);
        g.setColour (Colours::white);
        g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void FlpPage::Model::paintListBoxItem (int row, Graphics& g, int w, int h, bool)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (4, 3);
    if (row % 2 == 0) { g.setColour (Colours::white.withAlpha (0.025f)); g.fillRoundedRectangle (r, 10); }
    auto check = Rectangle<float> (18, 18).withCentre ({ r.getX() + 16, r.getCentreY() });
    auto playR = Rectangle<float> (30, 30).withCentre ({ r.getRight() - 20, r.getCentreY() });

    if (! samples)
    {
        if (! isPositiveAndBelow (row, (int) page.patRows.size())) return;
        auto* p = page.patRows[(size_t) row];
        const bool playing = page.playingPattern == p->id && page.ctx.p.loopingSample() != nullptr;
        if (playing)
        {
            g.setColour (col::violet.withAlpha (0.2f));
            g.fillRoundedRectangle (r, 10);
            // tête de lecture
            const float pos = page.ctx.p.playingSample() == page.playingAudio ? page.ctx.p.playingPosition() : -1.0f;
            if (pos >= 0)
            {
                auto bar = r.withTrimmedLeft (36).withTrimmedRight (50).removeFromBottom (3).translated (0, -4);
                g.setColour (Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (bar, 1.5f);
                g.setColour (col::magenta);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos), 1.5f);
            }
        }
        drawCheck (g, check, page.selPatterns.count (p->id) > 0, col::layer (row % 4).darker (0.2f));
        std::set<int> chans;
        for (auto& n : p->notes) chans.insert (n.channel);
        g.setFont (font (13.0f, true));
        g.setColour (col::text);
        g.drawText (p->name.isNotEmpty() ? p->name : "Pattern " + String (p->id), r.withTrimmedLeft (36).withTrimmedRight (170), Justification::centredLeft, true);
        g.setFont (font (11.0f));
        g.setColour (col::text3);
        auto info = String ((int) p->notes.size()) + String::fromUTF8 (" notes  ·  ") + String ((int) chans.size()) + " instr.";
        auto it = page.renders.find (p->id);
        if (it != page.renders.end() && it->second.channelsSilent > 0)
            info << String::fromUTF8 ("  ·  ") << it->second.channelsSilent << " muet" << (it->second.channelsSilent > 1 ? "s" : "");
        g.drawText (info, r.withTrimmedRight (46), Justification::centredRight);

        g.setColour ((playing ? col::magenta : col::violet).withAlpha (playing ? 0.9f : 0.35f));
        g.fillEllipse (playR);
        drawIcon (g, playing ? Pill::stop : Pill::play, playR.reduced (8), Colours::white);
        return;
    }

    if (! isPositiveAndBelow (row, (int) page.smpRows.size())) return;
    auto* c = page.smpRows[(size_t) row];
    const bool found = c->resolved.existsAsFile();
    drawCheck (g, check, page.selChannels.count (c->id) > 0, found ? col::violet : col::red.withAlpha (0.6f));
    auto dot = Rectangle<float> (6, 6).withCentre ({ r.getX() + 38, r.getCentreY() });
    g.setColour (found ? col::green : col::red);
    g.fillEllipse (dot);
    g.setFont (font (13.0f, true));
    g.setColour (col::text);
    g.drawText (c->displayName(), r.withTrimmedLeft (48).withTrimmedRight (46).withHeight (r.getHeight() * 0.55f), Justification::bottomLeft, true);
    g.setFont (font (10.5f));
    g.setColour (found ? col::text3 : col::red.withAlpha (0.7f));
    const auto fname = c->samplePath.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
    g.drawText (found ? fname : String::fromUTF8 ("introuvable  ·  ") + fname, r.withTrimmedLeft (48).withTrimmedRight (46).withTrimmedTop (r.getHeight() * 0.55f), Justification::topLeft, true);
    if (found)
    {
        g.setColour (col::violet.withAlpha (0.35f));
        g.fillEllipse (playR);
        drawIcon (g, Pill::play, playR.reduced (8), Colours::white);
    }
}

SamplePtr FlpPage::loadCached (const File& f)
{
    auto it = cache.find (f.getFullPathName());
    if (it != cache.end()) return it->second;
    String err;
    auto s = loadAudioFile (f, err);
    if (s != nullptr) cache[f.getFullPathName()] = s;
    return s;
}

void FlpPage::togglePattern (const FlpPattern* p)
{
    if (playingPattern == p->id && ctx.p.loopingSample() != nullptr)
    {
        ctx.p.stopAll();
        playingPattern = -1;
        midiList.repaint();
        return;
    }
    auto it = renders.find (p->id);
    if (it == renders.end())
        it = renders.emplace (p->id, renderPattern (*ctx.p.flp, *p, [this] (const File& f) { return loadCached (f); })).first;
    auto& rnd = it->second;
    if (rnd.audio == nullptr || rnd.channelsPlayed == 0)
    {
        ctx.toast.show (String::fromUTF8 ("Aucun sample trouvé pour ce pattern (instruments = plugins/synthés)"), col::pink);
        return;
    }
    playingPattern = p->id;
    playingAudio = rnd.audio.get();
    ctx.p.previewLoop (rnd.audio);
    String msg = String::fromUTF8 ("▶  ") + rnd.audio->name + String::fromUTF8 ("  ·  ") + String (ctx.p.flp->tempo, 0) + " BPM";
    if (rnd.channelsSilent > 0) msg << String::fromUTF8 ("  ·  ") << rnd.channelsSilent << String::fromUTF8 (" instrument(s) sans sample (synthés)");
    ctx.toast.show (msg);
    midiList.repaint();
}

void FlpPage::Model::listBoxItemClicked (int row, const MouseEvent& e)
{
    const bool inCheck = e.x < 40;
    if (! samples)
    {
        if (! isPositiveAndBelow (row, (int) page.patRows.size())) return;
        auto* p = page.patRows[(size_t) row];
        if (inCheck)
        {
            if (page.selPatterns.count (p->id)) page.selPatterns.erase (p->id); else page.selPatterns.insert (p->id);
            page.midiList.repaintRow (row);
            page.updateExtractLabel();
        }
        else page.togglePattern (p);
        return;
    }
    if (! isPositiveAndBelow (row, (int) page.smpRows.size())) return;
    auto* c = page.smpRows[(size_t) row];
    if (inCheck)
    {
        if (page.selChannels.count (c->id)) page.selChannels.erase (c->id); else page.selChannels.insert (c->id);
        page.smpList.repaintRow (row);
        page.updateExtractLabel();
        return;
    }
    if (c->resolved.existsAsFile())
        if (auto s = page.loadCached (c->resolved)) { page.playingPattern = -1; page.ctx.p.preview (s); page.midiList.repaint(); }
}

void FlpPage::Model::listBoxItemDoubleClicked (int row, const MouseEvent& e)
{
    if (! samples || e.x < 40 || ! isPositiveAndBelow (row, (int) page.smpRows.size())) return;
    auto* c = page.smpRows[(size_t) row];
    if (! c->resolved.existsAsFile()) return;
    int idx = page.ctx.p.firstEmptyLayer();
    if (idx < 0) idx = kNumLayers - 1;
    if (page.ctx.p.loadLayer (idx, c->resolved).isEmpty())
    {
        if (page.onLayersChanged) page.onLayersChanged();
        page.ctx.toast.show (c->displayName() + String::fromUTF8 ("  →  layer ") + String (idx + 1));
    }
}

void FlpPage::updateExtractLabel()
{
    int pats = 0, smps = 0;
    for (auto* p : patRows) pats += selPatterns.count (p->id) ? 1 : 0;
    for (auto* c : smpRows) smps += (selChannels.count (c->id) && c->resolved.existsAsFile()) ? 1 : 0;
    const bool everything = pats == (int) patRows.size() && (int) selChannels.size() == (int) smpRows.size();
    extractBtn.setButtonText (everything ? String ("EXTRAIRE TOUT")
                                         : "EXTRAIRE " + String (pats) + " PATTERN" + (pats > 1 ? "S" : "") + String::fromUTF8 (" · ") + String (smps) + " SAMPLE" + (smps > 1 ? "S" : ""));
    extractBtn.setEnabled (pats + smps > 0);
    allPat.setButtonText ((int) selPatterns.size() == (int) patRows.size() ? "AUCUN" : "TOUT");
    allSmp.setButtonText ((int) selChannels.size() == (int) smpRows.size() ? "AUCUN" : "TOUT");
}

void FlpPage::refresh()
{
    const bool loaded = ctx.p.flp != nullptr;
    if (shownProject != ctx.p.flp.get())
    {
        // nouveau projet : tout est coché par défaut
        selPatterns.clear(); selChannels.clear(); renders.clear(); playingPattern = -1;
        if (loaded)
        {
            for (auto& [id, p] : ctx.p.flp->patterns) if (! p.notes.empty()) selPatterns.insert (id);
            for (auto& [id, c] : ctx.p.flp->channels) if (c.samplePath.isNotEmpty()) selChannels.insert (id);
        }
    }
    shownProject = ctx.p.flp.get();
    patRows.clear(); smpRows.clear();
    if (loaded)
    {
        for (auto& [id, p] : ctx.p.flp->patterns) if (! p.notes.empty()) patRows.push_back (&p);
        for (auto& [id, c] : ctx.p.flp->channels) if (c.samplePath.isNotEmpty()) smpRows.push_back (&c);
        std::stable_sort (smpRows.begin(), smpRows.end(), [] (auto* a, auto* b) { return a->resolved.existsAsFile() > b->resolved.existsAsFile(); });
    }
    midiList.updateContent(); smpList.updateContent();
    for (Component* comp : std::initializer_list<Component*> { &midiList, &smpList, &extractBtn, &searchBtn, &otherBtn, &allPat, &allSmp })
        comp->setVisible (loaded);
    updateExtractLabel();
    midiList.repaint(); smpList.repaint();
    repaint();
}

bool FlpPage::goBack()
{
    if (ctx.p.flp == nullptr) return false;
    ctx.p.stopAll();
    ctx.p.flp.reset();
    refresh();
    ctx.toast.show (String::fromUTF8 ("↶  Projet fermé"), col::lilac);
    return true;
}

void FlpPage::resized()
{
    auto r = getLocalBounds();
    dropZone = r.withSizeKeepingCentre (640, 340);
    infoArea = r.removeFromTop (92);
    r.removeFromTop (12);
    auto buttons = r.removeFromBottom (56);
    r.removeFromBottom (12);
    midiCard = r.removeFromLeft ((r.getWidth() - 16) / 2);
    r.removeFromLeft (16);
    smpCard = r;
    midiList.setBounds (midiCard.reduced (10, 10).withTrimmedTop (34));
    smpList.setBounds (smpCard.reduced (10, 10).withTrimmedTop (34));
    allPat.setBounds (midiCard.reduced (14, 10).removeFromTop (26).removeFromRight (70));
    allSmp.setBounds (smpCard.reduced (14, 10).removeFromTop (26).removeFromRight (70));
    otherBtn.setBounds (buttons.removeFromLeft (130).withSizeKeepingCentre (130, 38));
    buttons.removeFromLeft (8);
    searchBtn.setBounds (buttons.removeFromLeft (230).withSizeKeepingCentre (230, 38));
    extractBtn.setBounds (buttons.removeFromRight (340));
}

void FlpPage::tick (float dt)
{
    if (ctx.p.flp.get() != shownProject) refresh(); // projet remplacé ailleurs

    if (playingPattern >= 0)
    {
        if (ctx.p.loopingSample() == nullptr) { playingPattern = -1; midiList.repaint(); }
        else midiList.repaint();
    }

    const float nd = approach (drop, dragOver ? 1.0f : 0.0f, dt, 14.0f);
    const float na = approach (appear, 1.0f, dt, 6.0f);
    const bool anim = ctx.p.flp == nullptr && isShowing();
    if (anim) dash += dt * 16.0f;
    if (std::abs (nd - drop) > 0.002f || std::abs (na - appear) > 0.002f || anim)
    {
        drop = nd; appear = na;
        repaint (ctx.p.flp == nullptr ? dropZone.expanded (40) : getLocalBounds());
    }
}

void FlpPage::paint (Graphics& g)
{
    const float t = AnimHub::globalTime;
    if (ctx.p.flp == nullptr)
    {
        auto z = dropZone.toFloat();
        g.setColour (Colours::white.withAlpha (0.025f + 0.05f * drop));
        g.fillRoundedRectangle (z, 28.0f);
        dashedRect (g, z.reduced (1), 28.0f, dash, col::violetHi.withAlpha (0.3f + 0.5f * drop), 1.8f);

        // "disquette" FLP animée
        {
            const float bob = std::sin (t * 2.0f) * 4.0f;
            auto doc = Rectangle<float> (86, 100).withCentre (z.getCentre().translated (0, -40 + bob));
            Graphics::ScopedSaveState ss (g);
            g.addTransform (AffineTransform::rotation (std::sin (t * 1.3f) * 0.06f, doc.getCentreX(), doc.getCentreY()));
            Path p; p.addRoundedRectangle (doc, 14);
            drawGlowPath (g, p, col::violet, 1.0f, 0.35f + 0.3f * drop);
            g.setGradientFill (ColourGradient (col::violetHi, doc.getX(), doc.getY(), col::magenta, doc.getRight(), doc.getBottom(), false));
            g.fillPath (p);
            g.setColour (Colours::white.withAlpha (0.9f));
            g.setFont (font (24.0f, true));
            g.drawText ("FLP", doc.withTrimmedTop (34), Justification::centredTop);
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillRoundedRectangle (doc.getX() + 18, doc.getY() + 14, doc.getWidth() - 36, 14, 4);
        }

        g.setFont (font (18.0f, true));
        g.setColour (col::text);
        g.drawText (drop > 0.5f ? String::fromUTF8 ("Lâche ton projet !") : "Drop ton projet FL Studio (.flp)", z.withTrimmedTop (z.getHeight() * 0.62f).withHeight (24), Justification::centred);
        g.setFont (font (12.0f));
        g.setColour (col::text3);
        g.drawText (String::fromUTF8 ("K1T en sort tous les MIDI (par pattern et par instrument) + tous les samples utilisés"), z.withTrimmedTop (z.getHeight() * 0.62f + 28).withHeight (18), Justification::centred);
        g.drawText ("ou clique pour choisir un fichier", z.withTrimmedTop (z.getHeight() * 0.62f + 48).withHeight (18), Justification::centred);
        return;
    }

    auto& p = *ctx.p.flp;
    auto ia = infoArea.toFloat();
    drawCard (g, ia, 22.0f);
    auto inner = infoArea.reduced (22, 14);
    g.setFont (fontSpaced (10.0f, true, 0.18f));
    g.setColour (col::text3);
    g.drawText ("PROJET", inner.removeFromTop (16), Justification::centredLeft);
    auto titleRow = inner.removeFromTop (34);
    g.setFont (font (24.0f, true));
    g.setGradientFill (ColourGradient (Colours::white, (float) titleRow.getX(), 0, col::lilac, (float) titleRow.getX() + 400, 0, false));
    g.drawText (p.title, titleRow, Justification::centredLeft, true);

    StringArray chips;
    chips.add (String (p.tempo, p.tempo == std::floor (p.tempo) ? 0 : 2) + " BPM");
    chips.add (String (p.numPatternsWithNotes()) + (p.numPatternsWithNotes() > 1 ? " patterns" : " pattern"));
    chips.add (String (p.numNotes()) + " notes");
    chips.add (String (p.numSamplesFound()) + "/" + String (p.numSamples()) + " samples");
    if (p.version.isNotEmpty()) chips.add ("FL " + p.version.upToFirstOccurrenceOf (".", false, false) + "." + p.version.fromFirstOccurrenceOf (".", false, false).upToFirstOccurrenceOf (".", false, false));
    chips.add ("PPQ " + String (p.ppq));
    auto chipRow = inner.removeFromTop (24).toFloat();
    float x = chipRow.getX();
    g.setFont (font (11.0f, true));
    for (int i = 0; i < chips.size(); ++i)
    {
        const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), chips[i]) + 22.0f;
        auto r = Rectangle<float> (x, chipRow.getY(), w, 22);
        const auto c = i == 3 ? (p.numSamplesFound() == p.numSamples() ? col::green : col::pink) : col::violetHi;
        g.setColour (c.withAlpha (0.14f));
        g.fillRoundedRectangle (r, 11);
        g.setColour (c.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 11, 1);
        g.setColour (c.brighter (0.3f));
        g.drawText (chips[i], r, Justification::centred);
        x += w + 8;
    }

    drawCard (g, midiCard.toFloat(), 22.0f);
    drawCard (g, smpCard.toFloat(), 22.0f);
    sectionTitle (g, midiCard.reduced (20, 12).removeFromTop (22), String::fromUTF8 ("PATTERNS"));
    sectionTitle (g, smpCard.reduced (20, 12).removeFromTop (22), String::fromUTF8 ("SAMPLES"));
    g.setFont (font (10.5f));
    g.setColour (col::text3);
    g.drawText (String::fromUTF8 ("▶ = écouter avec les vrais sons  ·  ☑ = à exporter"), midiCard.reduced (20, 12).removeFromTop (22).withTrimmedLeft (78).withTrimmedRight (70), Justification::centredLeft, true);
    g.drawText (String::fromUTF8 ("▶ = écouter  ·  double-clic = layer"), smpCard.reduced (20, 12).removeFromTop (22).withTrimmedLeft (78).withTrimmedRight (70), Justification::centredLeft, true);
    if (patRows.empty())
        g.drawText (String::fromUTF8 ("Aucune note trouvée dans les patterns"), midiCard, Justification::centred);
    if (smpRows.empty())
        g.drawText ("Aucun sample dans ce projet", smpCard, Justification::centred);
}

void FlpPage::mouseUp (const MouseEvent& e)
{
    if (ctx.p.flp == nullptr && dropZone.contains (e.getPosition())) browse();
}

void FlpPage::browse()
{
    chooser = std::make_unique<FileChooser> ("Projet FL Studio", File(), "*.flp");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) load (fc.getResult());
    });
}

bool FlpPage::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (File (f).hasFileExtension ("flp")) return true;
    return false;
}

void FlpPage::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files) if (File (f).hasFileExtension ("flp")) { load (File (f)); break; }
}

void FlpPage::load (const File& f)
{
    auto proj = std::make_unique<FlpProject>();
    String err;
    if (! parseFlp (f, *proj, err)) { ctx.toast.show (err, col::red); return; }
    resolveSamples (*proj, ctx.p.flpSearchDir);
    ctx.p.stopAll();
    ctx.p.flp = std::move (proj);
    cache.clear();
    appear = 0;
    refresh();
    ctx.burstAt (*this, infoArea.getCentre().toFloat(), 30);
    ctx.toast.show (String (ctx.p.flp->numPatternsWithNotes()) + String::fromUTF8 (" patterns  ·  ") + String (ctx.p.flp->numSamples()) + String::fromUTF8 (" samples détectés"));
}

void FlpPage::extract()
{
    if (ctx.p.flp == nullptr) return;
    chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Où exporter ?"), ctx.p.flp->file.getParentDirectory());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
    {
        auto dir = fc.getResult();
        if (! dir.isDirectory() || ctx.p.flp == nullptr) return;
        const bool allPats = selPatterns.size() == patRows.size(), allSmps = selChannels.size() == smpRows.size();
        auto res = exportFlp (*ctx.p.flp, dir, allPats ? nullptr : &selPatterns, allSmps ? nullptr : &selChannels);
        ctx.burstAt (extractBtn, extractBtn.getLocalBounds().getCentre().toFloat(), 60);
        String msg = String (res.midiFiles) + String::fromUTF8 (" MIDI  ·  ") + String (res.samplesCopied) + String::fromUTF8 (" samples exportés");
        if (res.samplesMissing > 0) msg << String::fromUTF8 ("  ·  ") << res.samplesMissing << " introuvables";
        ctx.toast.show (msg, res.samplesMissing > 0 ? col::pink : col::green);
        res.folder.revealToUser();
    });
}
} // namespace k1t::ui
