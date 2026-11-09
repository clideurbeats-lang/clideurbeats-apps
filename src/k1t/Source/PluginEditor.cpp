#include "PluginEditor.h"

using namespace juce;
using namespace k1t;
using namespace k1t::ui;

//==============================================================================
K1TLookAndFeel::K1TLookAndFeel()
{
    setColour (ScrollBar::thumbColourId, col::lilac.withAlpha (0.35f));
    setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    setColour (PopupMenu::backgroundColourId, col::bg2);
    setColour (PopupMenu::highlightedBackgroundColourId, col::violet);
    setColour (TooltipWindow::backgroundColourId, col::bg2);
}

void K1TLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int thumbStart, int thumbSize, bool over, bool down)
{
    Rectangle<float> thumb = vertical ? Rectangle<float> ((float) x + 1, (float) thumbStart, (float) w - 2, (float) thumbSize)
                                      : Rectangle<float> ((float) thumbStart, (float) y + 1, (float) thumbSize, (float) h - 2);
    g.setColour (col::lilac.withAlpha (down ? 0.6f : (over ? 0.45f : 0.22f)));
    g.fillRoundedRectangle (thumb.reduced (vertical ? 0.5f : 2.0f, vertical ? 2.0f : 0.5f), 3.0f);
}

//==============================================================================
// Mises à jour
UpdatePanel::UpdatePanel (Ctx& c) : ctx (c)
{
    setInterceptsMouseClicks (true, true);
    primary.setWantsKeyboardFocus (false);
    secondary.setWantsKeyboardFocus (false);
    primary.textSize = 12.0f;
    primary.onClick = [this]
    {
        if (mode == available)
        {
            String err;
            if (! update::launch (candidate, err)) { ctx.toast.show (err, col::red); return; }
            setMode (running);
        }
        else if (mode == failed) update::logFile().revealToUser();
        else hide();
    };
    secondary.onClick = [this] { hide(); };
    addAndMakeVisible (primary);
    addAndMakeVisible (secondary);
    y.snap (-140.0f);
    y.stiffness = 220.0f;
    y.damping = 22.0f;
    setVisible (false);

    // une mise à jour était peut-être déjà en cours (fenêtre refermée entre-temps)
    progress = update::readProgress();
    const auto age = Time::getCurrentTime() - update::logFile().getLastModificationTime();
    if (progress.state == update::Progress::running && age.inMinutes() < 20) { setMode (running); show(); }
    else if (progress.state == update::Progress::done && update::compareVersions (progress.label, update::currentVersion()) > 0)
    {
        candidate.version = progress.label;
        setMode (done); show();
    }
    else checkNow (false);
}

UpdatePanel::~UpdatePanel() { alive->store (false); stopTimer(); }

void UpdatePanel::checkNow (bool userAsked)
{
    if (mode == running) { show(); return; }
    auto flag = alive;
    SafePointer<UpdatePanel> safe (this);
    Thread::launch ([flag, safe, userAsked]
    {
        Thread::sleep (userAsked ? 0 : 1200);
        if (! flag->load()) return;
        auto c = update::findUpdate();
        auto web = update::checkOnline();
        if (web.isValid() && (! c.isValid() || update::compareVersions (web.version, c.version) > 0)) c = web;
        MessageManager::callAsync ([safe, c, userAsked]
        {
            if (safe == nullptr) return;
            if (c.isValid()) safe->offer (c, userAsked);
            else if (userAsked)
                safe->ctx.toast.show (String::fromUTF8 ("K1T est à jour  ·  v") + update::currentVersion(), col::green);
        });
    });
}

void UpdatePanel::offer (const update::Candidate& c, bool)
{
    if (mode == running) return;
    candidate = c;
    setMode (available);
    show();
}

void UpdatePanel::setMode (Mode m)
{
    mode = m;
    switch (m)
    {
        case available: primary.setButtonText (String::fromUTF8 ("METTRE À JOUR")); primary.setVisible (true); secondary.setVisible (true);
                        secondary.setButtonText ("PLUS TARD"); break;
        case running:   primary.setVisible (false); secondary.setVisible (true); secondary.setButtonText ("MASQUER");
                        startTimerHz (2); break;
        case done:      primary.setButtonText ("OK"); primary.setVisible (true); secondary.setVisible (false); stopTimer(); break;
        case failed:    primary.setButtonText ("VOIR LE LOG"); primary.setVisible (true); secondary.setVisible (true);
                        secondary.setButtonText ("FERMER"); stopTimer(); break;
        case none: default: break;
    }
    resized();
    repaint();
    if (onStateChanged) onStateChanged();
}

void UpdatePanel::timerCallback()
{
    progress = update::readProgress();
    if (progress.state == update::Progress::done)
    {
        ctx.particles.burst (getParentComponent() != nullptr ? getBounds().getCentre().toFloat() : Point<float>(), 60);
        setMode (done);
        show();
    }
    else if (progress.state == update::Progress::failed) { setMode (failed); show(); }
    repaint();
}

void UpdatePanel::show() { visibleTarget = true; setVisible (true); toFront (false); y.target = 0.0f; if (onStateChanged) onStateChanged(); }
void UpdatePanel::hide() { visibleTarget = false; y.target = -140.0f; if (onStateChanged) onStateChanged(); }

void UpdatePanel::tick (float dt)
{
    if (! isVisible()) return;
    const bool moving = y.step (dt);
    pulse += dt;
    const float target = progress.fraction();
    shownFraction = approach (shownFraction, mode == done ? 1.0f : target, dt, 4.0f);
    setTransform (AffineTransform::translation (0, y.pos));
    if (! visibleTarget && ! moving) setVisible (false);
    if (mode == running || moving) repaint();
}

void UpdatePanel::resized()
{
    auto r = getLocalBounds().reduced (18, 0).withTrimmedTop (14).withTrimmedBottom (14);
    auto right = r.removeFromRight (mode == available || mode == failed ? 290 : 130);
    if (secondary.isVisible() && primary.isVisible())
    {
        primary.setBounds (right.removeFromRight (160).withSizeKeepingCentre (160, 38));
        right.removeFromRight (8);
        secondary.setBounds (right.withSizeKeepingCentre (right.getWidth(), 34));
    }
    else if (primary.isVisible()) primary.setBounds (right.removeFromRight (110).withSizeKeepingCentre (110, 38));
    else secondary.setBounds (right.removeFromRight (110).withSizeKeepingCentre (110, 34));
}

void UpdatePanel::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::bg0.withAlpha (0.94f));
    g.fillRoundedRectangle (b, 20.0f);
    drawCard (g, b, 20.0f, mode == available ? 0.4f + 0.3f * std::sin (pulse * 3.0f) * 0.5f + 0.15f : 0.3f,
              mode == failed ? col::red : (mode == done ? col::green : col::magenta));

    auto r = getLocalBounds().reduced (22, 14);
    r.removeFromRight (mode == available || mode == failed ? 300 : 140);
    auto ic = r.removeFromLeft (40).toFloat().withSizeKeepingCentre (34, 34);
    g.setColour ((mode == failed ? col::red : (mode == done ? col::green : col::violet)).withAlpha (0.25f));
    g.fillEllipse (ic);
    drawIcon (g, mode == done ? Pill::sparkle : Pill::refresh, ic.reduced (6), col::text);
    r.removeFromLeft (12);

    String title, sub;
    switch (mode)
    {
        case available: title = String::fromUTF8 ("Mise à jour K1T v") + candidate.version + String::fromUTF8 (" prête");
                        sub = (candidate.fromWeb ? String::fromUTF8 ("Téléchargée depuis le site @clideurbeats")
                                                 : String::fromUTF8 ("Trouvée dans ") + candidate.script.getParentDirectory().getFileName())
                            + String::fromUTF8 (" · s'installe toute seule, sans Terminal"); break;
        case running:   title = String::fromUTF8 ("Mise à jour en cours…");
                        sub = progress.label.isNotEmpty() ? progress.label : String::fromUTF8 ("Préparation");
                        if (progress.buildPercent >= 0) sub << "  " << roundToInt (progress.buildPercent * 100) << " %";
                        sub << String::fromUTF8 ("  ·  tu peux continuer à bosser"); break;
        case done:      title = String::fromUTF8 ("K1T v") + (candidate.version.isNotEmpty() ? candidate.version : progress.label) + String::fromUTF8 (" installé ✓");
                        sub = progress.viaPkg ? String::fromUTF8 ("Termine dans l'installeur qui s'est ouvert, puis recharge K1T dans FL")
                                              : String::fromUTF8 ("Ferme la fenêtre de K1T et recharge-le dans FL (ou relance FL)"); break;
        case failed:    title = String::fromUTF8 ("La mise à jour a échoué");
                        sub = progress.error.isNotEmpty() ? progress.error : String::fromUTF8 ("Voir le log"); break;
        case none: default: break;
    }
    auto tr = r.removeFromTop (r.getHeight() / 2 + 2);
    g.setFont (font (15.0f, true));
    g.setColour (col::text);
    g.drawText (title, tr, Justification::bottomLeft, true);
    g.setFont (font (11.5f));
    g.setColour (col::text2);
    g.drawText (sub, r.removeFromTop (20), Justification::centredLeft, true);

    if (mode == running || mode == done)
    {
        auto bar = r.removeFromBottom (6).toFloat().withTrimmedRight (10);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 3.0f);
        auto fillR = bar.withWidth (jmax (6.0f, bar.getWidth() * shownFraction));
        g.setGradientFill (ColourGradient (col::violet, fillR.getX(), 0, col::magenta, fillR.getRight(), 0, false));
        g.fillRoundedRectangle (fillR, 3.0f);
        // reflet qui défile
        if (mode == running)
        {
            const float x = fillR.getX() + std::fmod (pulse * 120.0f, fillR.getWidth() + 60.0f) - 30.0f;
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), x - 30, 0, Colours::white.withAlpha (0.0f), x + 30, 0, false));
            ColourGradient sh (Colours::white.withAlpha (0.0f), x - 30, 0, Colours::white.withAlpha (0.0f), x + 30, 0, false);
            sh.addColour (0.5, Colours::white.withAlpha (0.45f));
            g.setGradientFill (sh);
            g.saveState();
            g.reduceClipRegion (fillR.toNearestInt());
            g.fillRect (fillR);
            g.restoreState();
        }
    }
}

//==============================================================================
K1TEditor::K1TEditor (K1TProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    pages = { &mix, &gen, &flp };
    for (auto* pg : pages) addChildComponent (pg);

    tabs.onChange = [this] (int t) { showTab (t, true); };
    addAndMakeVisible (tabs);

    addAndMakeVisible (particles);
    addAndMakeVisible (toast);

    addChildComponent (updates);
    updPill.setWantsKeyboardFocus (false);
    updPill.textSize = 10.5f;
    updPill.onClick = [this] { updates.show(); };
    addChildComponent (updPill);
    updates.onStateChanged = [this] { updPill.setVisible (updates.hasPending()); };

    backBtn.setWantsKeyboardFocus (false);
    backBtn.textSize = 10.5f;
    backBtn.setTooltip (String::fromUTF8 ("Revenir en arrière (Cmd+Z)"));
    backBtn.onClick = [this] { goBack(); };
    addAndMakeVisible (backBtn);

    gen.onLayersChanged = [this] { mix.refresh(); };
    flp.onLayersChanged = [this] { mix.refresh(); };

    proc.addChangeListener (this);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);

    setSize (1080, 720);
    mix.kitChanged();
    showTab (proc.currentTab, false);
    renderBlobs();

    vblank = std::make_unique<VBlankAttachment> (this, [] { AnimHub::tick(); });
}

K1TEditor::~K1TEditor()
{
    vblank.reset();
    removeMouseListener (this);
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void K1TEditor::parentHierarchyChanged()
{
    if (isShowing()) grabKeyboardFocus();
}

void K1TEditor::mouseDown (const MouseEvent&)
{
    if (! hasKeyboardFocus (true)) grabKeyboardFocus();
}

void K1TEditor::mouseUp (const MouseEvent& e)
{
    if (e.eventComponent == this && versionArea.contains (e.getPosition()))
        updates.checkNow (true);
}

bool K1TEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (File (f).hasFileExtension ("sh") && File (f).getFileName().containsIgnoreCase ("k1t")) return true;
    return false;
}

void K1TEditor::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
    {
        const File file (f);
        const auto v = update::readScriptVersion (file);
        if (v.isEmpty()) continue;
        if (update::compareVersions (v, update::currentVersion()) <= 0)
        {
            toast.show (String::fromUTF8 ("Tu as déjà K1T v") + update::currentVersion() + String::fromUTF8 (" (ce fichier est la v") + v + ")", col::lilac);
            return;
        }
        updates.offer ({ file, v }, true);
        return;
    }
}

void K1TEditor::changeListenerCallback (ChangeBroadcaster*)
{
    mix.kitChanged();
}

bool K1TEditor::canGoBack() const
{
    switch (current)
    {
        case 0:  return proc.canUndoMix();
        case 1:  return proc.canUndoGenerate();
        default: return proc.flp != nullptr;
    }
}

void K1TEditor::goBack()
{
    if (current == 0) mix.goBack();
    else if (current == 1) gen.goBack();
    else flp.goBack();
}

bool K1TEditor::keyPressed (const KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        goBack();
        return true;
    }
    if (k == KeyPress::spaceKey)
    {
        if (current == 0) mix.playKit();
        else if (current == 1) gen.playReference();
        return true;
    }
    if (k.getKeyCode() >= '1' && k.getKeyCode() <= '3' && ! k.getModifiers().isAnyModifierKeyDown())
    {
        showTab (k.getKeyCode() - '1', true);
        return true;
    }
    return false;
}

void K1TEditor::showTab (int t, bool animate)
{
    t = jlimit (0, 2, t);
    if (t == current && pages[(size_t) t]->isVisible()) return;
    previous = current == t ? -1 : current;
    current = t;
    proc.currentTab = t;
    tabs.setSelected (t);
    pages[(size_t) t]->setVisible (true);
    if (t == 0) mix.refresh();
    if (! animate || previous < 0)
    {
        transition = 1.0f;
        for (int i = 0; i < 3; ++i)
        {
            pages[(size_t) i]->setVisible (i == t);
            pages[(size_t) i]->setAlpha (1.0f);
            pages[(size_t) i]->setTransform ({});
        }
        previous = -1;
    }
    else transition = 0.0f;

    if (updates.isVisible()) updates.toFront (false);
    particles.toFront (false);
    toast.toFront (false);
    repaint (getLocalBounds().removeFromBottom (40));
}

void K1TEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (72);
    tabs.setBounds (header.withSizeKeepingCentre (330, 38));
    logoArea = header.withWidth (180).withTrimmedLeft (24);
    meterArea = header.removeFromRight (150).reduced (24, 20);
    content = Rectangle<int> (24, 80, getWidth() - 48, getHeight() - 80 - 44);
    for (auto* pg : pages) { pg->setTransform ({}); pg->setBounds (content); }
    particles.setBounds (getLocalBounds());
    toast.setBounds (getLocalBounds());
    updates.setBounds (getLocalBounds().withSizeKeepingCentre (720, 92).withY (80));
    updPill.setBounds (meterArea.getX() - 160, 20, 148, 32);
    backBtn.setBounds (tabs.getX() - 112, 20, 100, 32);
    versionArea = getLocalBounds().removeFromBottom (40).removeFromRight (90).withTrimmedRight (24);
}

//==============================================================================
void K1TEditor::renderBlobs()
{
    const int w = 108, h = 72;
    if (! blobs.isValid()) blobs = Image (Image::ARGB, w, h, true);
    Graphics g (blobs);
    g.fillAll (col::bg0);
    const float t = blobTime;
    struct B { float x, y, r; Colour c; };
    const B bs[] = {
        { 0.18f + 0.10f * std::sin (t * 0.21f), 0.22f + 0.12f * std::cos (t * 0.17f), 0.55f, Colour (0xff6d28d9).withAlpha (0.55f) },
        { 0.82f + 0.08f * std::cos (t * 0.19f), 0.30f + 0.15f * std::sin (t * 0.23f), 0.48f, Colour (0xffc026d3).withAlpha (0.34f) },
        { 0.55f + 0.18f * std::sin (t * 0.13f + 1.0f), 0.92f + 0.08f * std::cos (t * 0.16f), 0.62f, Colour (0xff4f46e5).withAlpha (0.36f) },
        { 0.40f + 0.20f * std::cos (t * 0.11f + 2.0f), 0.50f + 0.10f * std::sin (t * 0.27f), 0.32f, Colour (0xffa78bfa).withAlpha (0.14f) },
    };
    for (auto& b : bs)
    {
        const float cx = b.x * w, cy = b.y * h, rad = b.r * w;
        g.setGradientFill (ColourGradient (b.c, cx, cy, b.c.withAlpha (0.0f), cx + rad, cy, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
    }
}

void K1TEditor::tick (float dt)
{
    // transition entre pages
    if (transition < 1.0f)
    {
        transition = jmin (1.0f, transition + dt / 0.34f);
        const float e = 1.0f - std::pow (1.0f - transition, 3.0f);
        const float dir = previous >= 0 && current > previous ? 1.0f : -1.0f;
        auto* in = pages[(size_t) current];
        in->setAlpha (e);
        in->setTransform (AffineTransform::translation (dir * 34.0f * (1.0f - e), 0));
        if (previous >= 0)
        {
            auto* out = pages[(size_t) previous];
            out->setAlpha (1.0f - e);
            out->setTransform (AffineTransform::translation (-dir * 34.0f * e, 0));
            if (transition >= 1.0f) { out->setVisible (false); out->setTransform ({}); out->setAlpha (1.0f); }
        }
        if (transition >= 1.0f) { in->setTransform ({}); previous = -1; }
    }

    // fond animé : il bouge très lentement et il est flou, 5 i/s suffisent (économise le CPU dans FL)
    blobTime += dt;
    blobAccum += dt;
    if (blobAccum > 1.0f / 5.0f)
    {
        blobAccum = 0;
        renderBlobs();
        repaint();
    }

    const float m = approach (meter, proc.outputLevel(), dt, 18.0f);
    if (std::abs (m - meter) > 0.002f) { meter = m; repaint (meterArea.expanded (4)); }

    logoGlow += dt;
    repaint (logoArea);

    const float backAlpha = canGoBack() ? 1.0f : 0.35f;
    if (std::abs (backBtn.getAlpha() - backAlpha) > 0.01f) backBtn.setAlpha (approach (backBtn.getAlpha(), backAlpha, dt, 10.0f));
}

void K1TEditor::drawLogo (Graphics& g, Rectangle<float> r)
{
    const float pulse = 0.5f + 0.5f * std::sin (logoGlow * 2.0f);
    auto txt = r.withHeight (36).withY (r.getY() + 10);
    g.setFont (fontSpaced (34.0f, true, -0.02f));

    // halo derrière le "1"
    const float kW = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "K");
    const float oneW = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "1");
    auto oneR = Rectangle<float> (txt.getX() + kW, txt.getY(), oneW, txt.getHeight());
    g.setGradientFill (ColourGradient (col::magenta.withAlpha (0.35f + 0.25f * pulse), oneR.getCentreX(), oneR.getCentreY(),
                                       col::magenta.withAlpha (0.0f), oneR.getCentreX() + 30, oneR.getCentreY(), true));
    g.fillEllipse (oneR.expanded (22, 12));

    g.setGradientFill (ColourGradient (Colours::white, txt.getX(), txt.getY(), col::lilac, txt.getX(), txt.getBottom(), false));
    g.drawText ("K", txt, Justification::centredLeft, false);
    g.setGradientFill (ColourGradient (col::magenta, oneR.getX(), oneR.getY(), col::violetHi, oneR.getRight(), oneR.getBottom(), false));
    g.drawText ("1", txt.withTrimmedLeft (kW), Justification::centredLeft, false);
    g.setGradientFill (ColourGradient (Colours::white, txt.getX(), txt.getY(), col::lilac, txt.getX(), txt.getBottom(), false));
    g.drawText ("T", txt.withTrimmedLeft (kW + oneW), Justification::centredLeft, false);

    g.setFont (fontSpaced (8.5f, true, 0.42f));
    g.setColour (col::text3);
    g.drawText ("DRUM KIT LAB", r.withY (r.getY() + 46).withHeight (12), Justification::centredLeft, false);
}

void K1TEditor::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.fillAll (col::bg0);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImage (blobs, b, RectanglePlacement::stretchToFit);

    // grain très léger + vignette
    g.setGradientFill (ColourGradient (Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                                       Colours::black.withAlpha (0.55f), 0, 0, true));
    g.fillRect (b);

    // header
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), 0, 72, Colours::white.withAlpha (0.0f), b.getWidth(), 72, false));
    {
        ColourGradient line (Colours::white.withAlpha (0.0f), 0, 72, Colours::white.withAlpha (0.0f), b.getWidth(), 72, false);
        line.addColour (0.5, col::lilac.withAlpha (0.18f));
        g.setGradientFill (line);
        g.fillRect (0.0f, 71.5f, b.getWidth(), 1.0f);
    }
    drawLogo (g, logoArea.toFloat());

    // meter
    {
        auto m = meterArea.toFloat();
        const int bars = 14;
        const float bw = 3.0f, gap = (m.getWidth() - 30.0f - bars * bw) / (bars - 1);
        g.setFont (fontSpaced (9.0f, true, 0.2f));
        g.setColour (col::text3);
        g.drawText ("OUT", m.removeFromLeft (28), Justification::centredLeft);
        m.removeFromLeft (2);
        const float lvl = jlimit (0.0f, 1.0f, std::pow (meter, 0.5f));
        for (int i = 0; i < bars; ++i)
        {
            const float frac = (float) i / (bars - 1);
            const float shape = 0.35f + 0.65f * std::sin ((frac * 0.9f + 0.05f) * MathConstants<float>::pi);
            const float h = jmax (3.0f, m.getHeight() * shape * jlimit (0.08f, 1.0f, lvl * (1.1f - 0.4f * frac) + 0.08f));
            auto bar = Rectangle<float> (m.getX() + i * (bw + gap), m.getCentreY() - h * 0.5f, bw, h);
            const bool lit = frac < lvl;
            g.setColour ((lit ? col::violetHi.interpolatedWith (col::magenta, frac) : Colours::white.withAlpha (0.12f)));
            g.fillRoundedRectangle (bar, 1.5f);
        }
    }

    // footer
    auto f = getLocalBounds().removeFromBottom (40).reduced (28, 0);
    g.setFont (fontSpaced (9.5f, true, 0.2f));
    g.setColour (col::text3.withAlpha (0.8f));
    static const char* hints[] = { "DROP  ·  MÉLANGE  ·  DÉCALE  ·  ENRICH  ·  GLISSE DANS FL",
                                   "1 RÉFÉRENCE  →  8 SONS NEUFS",
                                   "PROJET .FLP  →  MIDI + SAMPLES" };
    g.drawText (String::fromUTF8 (hints[current]), f, Justification::centred);
    g.setFont (font (10.0f));
    g.drawText ("ESPACE = PLAY", f.withTrimmedLeft (f.getWidth() - 300).withTrimmedRight (90), Justification::centredRight);
    g.setFont (fontSpaced (9.5f, true, 0.12f));
    g.setColour (col::text3.withAlpha (versionArea.contains (getMouseXYRelative()) ? 1.0f : 0.7f));
    auto ver = update::currentVersion();
    if (ver.endsWith (".0")) ver = ver.dropLastCharacters (2);
    g.drawText ("v" + ver, versionArea, Justification::centredRight);
}
