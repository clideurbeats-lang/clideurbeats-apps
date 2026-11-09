#pragma once
#include "Theme.h"
#include "Engine.h"

class K1TProcessor;

namespace k1t::ui
{
//==============================================================================
class Knob : public Slider, private Anim
{
public:
    Knob (const String& label, double mn, double mx, double def, Colour c = col::violetHi);
    std::function<String (double)> format;
    std::function<void (float)> onChange;
    void setSkewMid (double mid) { setSkewFactorFromMidPoint (mid); }
    void setColour (Colour c) { colour = c; repaint(); }
    void setValueQuiet (double v) { setValue (v, dontSendNotification); }

    void paint (Graphics&) override;
    void mouseEnter (const MouseEvent& e) override { Slider::mouseEnter (e); }
    String label;
    Colour colour;
    bool bipolar = false;
    float dim = 1.0f; // 0.35 quand le module est coupé

private:
    void tick (float dt) override;
    float shown = 0, hover = 0, glow = 0;
    double defaultValue;
};

//==============================================================================
class Switch : public Component, public SettableTooltipClient, private Anim
{
public:
    Switch() { setRepaintsOnMouseActivity (true); }
    std::function<void (bool)> onToggle;
    void setOn (bool b, bool notify = false);
    bool isOn() const { return on; }
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;
    Colour colour = col::violet;

private:
    void tick (float dt) override;
    bool on = false;
    Spring s;
    float hover = 0;
};

//==============================================================================
class Pill : public Button, private Anim
{
public:
    enum Style { primary, ghost, subtle };
    enum Icon { none, play, stop, cross, plus, sparkle, dice, arrowDown, folder, reverse, speaker, speakerOff, expand, refresh, undo };

    Pill (const String& text, Style st = ghost, Icon ic = none);
    void paintButton (Graphics&, bool over, bool down) override;
    void setIcon (Icon i) { ic = i; repaint(); }
    void setActive (bool a) { active = a; repaint(); }
    bool isActive() const { return active; }
    void setStyle (Style s) { style = s; repaint(); }
    Colour accent = col::violet;
    float textSize = 12.0f;

private:
    void tick (float dt) override;
    Style style;
    Icon ic;
    bool active = false;
    float hover = 0, press = 0, shimmer = -1;
};

void drawIcon (Graphics& g, Pill::Icon ic, Rectangle<float> r, Colour c);

//==============================================================================
class Segmented : public Component, private Anim
{
public:
    Segmented (StringArray items, bool large);
    std::function<void (int)> onChange;
    void setSelected (int i, bool notify = false);
    int getSelected() const { return selected; }
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override { hoverIdx = -1; repaint(); }

private:
    void tick (float dt) override;
    Rectangle<float> itemBounds (int i) const;
    StringArray items;
    bool large;
    int selected = 0, hoverIdx = -1;
    Spring x;
};

//==============================================================================
class WaveView : public Component, private Anim
{
public:
    explicit WaveView (K1TProcessor* proc = nullptr);
    void setSample (SamplePtr s, bool animate = true);
    SamplePtr getSample() const { return sample; }
    void paint (Graphics&) override;
    void resized() override { cacheW = -1; }
    void mouseUp (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;

    Colour colour = col::violetHi;
    bool compact = false;
    bool allowDragOut = false;
    std::function<void()> onClick;
    String dragName;
    String emptyText;

private:
    void tick (float dt) override;
    void rebuild();
    K1TProcessor* proc;
    SamplePtr sample;
    std::vector<float> mins, maxs;
    int cacheW = -1;
    float reveal = 1, head = -1, headAlpha = 0;
    bool dragging = false;
};

// Exporte et glisse un sample vers FL Studio / le Finder.
void dragSampleOut (K1TProcessor& p, const SampleData& s, const String& name, Component* source);

//==============================================================================
class Toast : public Component, private Anim
{
public:
    Toast() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }
    void show (const String& msg, Colour accent = col::violetHi);
    void paint (Graphics&) override;

private:
    void tick (float dt) override;
    String text;
    Colour accent;
    Spring y;
    double hideAt = 0;
};

//==============================================================================
class Particles : public Component, private Anim
{
public:
    Particles() { setInterceptsMouseClicks (false, false); }
    void burst (Point<float> at, int count = 36, Colour base = col::violetHi);
    void paint (Graphics&) override;

private:
    void tick (float dt) override;
    struct P { Point<float> pos, vel; float life, maxLife, size, rot, spin; Colour c; bool star; };
    std::vector<P> ps;
};

} // namespace k1t::ui
