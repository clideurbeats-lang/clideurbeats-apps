#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace k1t::ui
{
using namespace juce;

//==============================================================================
// DA violette
namespace col
{
    const Colour bg0      { 0xff09060f };
    const Colour bg1      { 0xff120c20 };
    const Colour bg2      { 0xff1b1230 };
    const Colour text     { 0xfff6f3ff };
    const Colour text2    { 0xffb3a6d4 };
    const Colour text3    { 0xff6f6390 };
    const Colour violet   { 0xff8b5cf6 };
    const Colour violetHi { 0xffa78bfa };
    const Colour lilac    { 0xffc4b5fd };
    const Colour magenta  { 0xffe879f9 };
    const Colour pink     { 0xfff472b6 };
    const Colour indigo   { 0xff818cf8 };
    const Colour mint     { 0xff5eead4 };
    const Colour green    { 0xff4ade80 };
    const Colour red      { 0xfff87171 };
    const Colour yellow   { 0xfffde68a };

    inline Colour layer (int i)
    {
        static const Colour c[] = { Colour (0xffa78bfa), Colour (0xffe879f9), Colour (0xff818cf8), Colour (0xfff9a8d4) };
        return c[(size_t) jlimit (0, 3, i)];
    }
}

//==============================================================================
inline String& typefaceName()
{
    static String name;
    static bool done = false;
    if (! done)
    {
        done = true;
        const auto all = Font::findAllTypefaceNames();
        for (auto cand : { "SF Pro Display", "SF Pro Text", "SF Pro", "Helvetica Neue", "Inter", "Segoe UI" })
            if (all.contains (cand)) { name = cand; break; }
    }
    return name;
}

inline Font font (float h, bool bold = false)
{
    FontOptions o = typefaceName().isNotEmpty() ? FontOptions (typefaceName(), h, bold ? Font::bold : Font::plain)
                                                : FontOptions (h, bold ? Font::bold : Font::plain);
    return Font (o);
}

inline Font fontSpaced (float h, bool bold, float kerning)
{
    return font (h, bold).withExtraKerningFactor (kerning);
}

//==============================================================================
// Animation : un hub unique piloté par la VBlank de l'éditeur.
struct Anim
{
    Anim();
    virtual ~Anim();
    virtual void tick (float dt) = 0;
};

struct AnimHub
{
    static void tick();
    static std::vector<Anim*>& list();
    static double& lastTime();
    static float globalTime;
};

inline float approach (float cur, float target, float dt, float speed = 14.0f)
{
    return cur + (target - cur) * (1.0f - std::exp (-dt * speed));
}

struct Spring
{
    float pos = 0, vel = 0, target = 0;
    float stiffness = 260.0f, damping = 24.0f;
    bool step (float dt)
    {
        dt = jmin (dt, 1.0f / 30.0f);
        const int sub = 4;
        for (int i = 0; i < sub; ++i)
        {
            const float h = dt / sub;
            const float a = stiffness * (target - pos) - damping * vel;
            vel += a * h;
            pos += vel * h;
        }
        const bool moving = std::abs (target - pos) > 0.0005f || std::abs (vel) > 0.0005f;
        if (! moving) { pos = target; vel = 0; }
        return moving;
    }
    void snap (float v) { pos = target = v; vel = 0; }
};

//==============================================================================
// Dessin
inline void drawCard (Graphics& g, Rectangle<float> r, float radius = 18.0f, float hover = 0.0f, Colour tint = col::violet)
{
    g.setColour (Colour (0xff160f26).withAlpha (0.72f));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.045f + hover * 0.03f), r.getX(), r.getY(),
                                       tint.withAlpha (0.02f + hover * 0.05f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.14f + hover * 0.08f), r.getX(), r.getY(),
                                       Colours::white.withAlpha (0.04f), r.getX(), r.getY() + 40.0f, false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    if (hover > 0.01f)
    {
        g.setColour (tint.withAlpha (0.35f * hover));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.2f);
    }
}

inline void drawGlowPath (Graphics& g, const Path& p, Colour c, float strokeW, float strength)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (c.withAlpha (strength * 0.10f * (4 - i)));
        g.strokePath (p, PathStrokeType (strokeW + i * 4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

inline void sectionTitle (Graphics& g, Rectangle<int> r, const String& s, Colour c = col::text3)
{
    g.setColour (c);
    g.setFont (fontSpaced (11.0f, true, 0.18f));
    g.drawText (s, r, Justification::centredLeft);
}

//==============================================================================
namespace icon
{
    inline Path play (Rectangle<float> r)
    {
        Path p;
        auto s = jmin (r.getWidth(), r.getHeight());
        auto b = r.withSizeKeepingCentre (s, s).reduced (s * 0.18f).translated (s * 0.05f, 0);
        p.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
        return p;
    }
    inline Path stop (Rectangle<float> r)
    {
        Path p;
        auto s = jmin (r.getWidth(), r.getHeight()) * 0.5f;
        p.addRoundedRectangle (r.withSizeKeepingCentre (s, s), s * 0.15f);
        return p;
    }
    inline Path cross (Rectangle<float> r)
    {
        Path p;
        auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.3f);
        p.startNewSubPath (b.getTopLeft()); p.lineTo (b.getBottomRight());
        p.startNewSubPath (b.getTopRight()); p.lineTo (b.getBottomLeft());
        return p;
    }
    inline Path plus (Rectangle<float> r)
    {
        Path p;
        auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.28f);
        p.startNewSubPath (b.getCentreX(), b.getY()); p.lineTo (b.getCentreX(), b.getBottom());
        p.startNewSubPath (b.getX(), b.getCentreY()); p.lineTo (b.getRight(), b.getCentreY());
        return p;
    }
    inline Path sparkle (Rectangle<float> r)
    {
        Path p;
        auto c = r.getCentre();
        const float R = jmin (r.getWidth(), r.getHeight()) * 0.5f, k = R * 0.22f;
        p.startNewSubPath (c.x, c.y - R);
        p.quadraticTo (c.x + k, c.y - k, c.x + R, c.y);
        p.quadraticTo (c.x + k, c.y + k, c.x, c.y + R);
        p.quadraticTo (c.x - k, c.y + k, c.x - R, c.y);
        p.quadraticTo (c.x - k, c.y - k, c.x, c.y - R);
        p.closeSubPath();
        return p;
    }
    inline Path dice (Rectangle<float> r)
    {
        Path p;
        auto s = jmin (r.getWidth(), r.getHeight()) * 0.7f;
        auto b = r.withSizeKeepingCentre (s, s);
        p.addRoundedRectangle (b, s * 0.22f);
        for (auto pt : { b.getCentre(), b.getRelativePoint (0.28f, 0.28f), b.getRelativePoint (0.72f, 0.72f) })
            p.addEllipse (Rectangle<float> (s * 0.16f, s * 0.16f).withCentre (pt));
        p.setUsingNonZeroWinding (false);
        return p;
    }
    inline Path arrowDown (Rectangle<float> r)
    {
        Path p;
        auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.25f);
        p.startNewSubPath (b.getCentreX(), b.getY()); p.lineTo (b.getCentreX(), b.getBottom() - 1);
        p.startNewSubPath (b.getX() + b.getWidth() * 0.18f, b.getCentreY() + b.getHeight() * 0.1f);
        p.lineTo (b.getCentreX(), b.getBottom());
        p.lineTo (b.getRight() - b.getWidth() * 0.18f, b.getCentreY() + b.getHeight() * 0.1f);
        return p;
    }
    inline Path reverse (Rectangle<float> r)
    {
        Path p;
        auto b = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (r.getHeight() * 0.28f);
        p.startNewSubPath (b.getRight(), b.getY() + b.getHeight() * 0.3f);
        p.lineTo (b.getX(), b.getY() + b.getHeight() * 0.3f);
        p.lineTo (b.getX() + b.getWidth() * 0.25f, b.getY());
        p.startNewSubPath (b.getX(), b.getY() + b.getHeight() * 0.7f);
        p.lineTo (b.getRight(), b.getY() + b.getHeight() * 0.7f);
        p.lineTo (b.getRight() - b.getWidth() * 0.25f, b.getBottom());
        return p;
    }
    inline Path folder (Rectangle<float> r)
    {
        Path p;
        auto s = jmin (r.getWidth(), r.getHeight());
        auto b = r.withSizeKeepingCentre (s, s * 0.78f).reduced (s * 0.16f);
        p.startNewSubPath (b.getX(), b.getBottom());
        p.lineTo (b.getX(), b.getY());
        p.lineTo (b.getX() + b.getWidth() * 0.38f, b.getY());
        p.lineTo (b.getX() + b.getWidth() * 0.48f, b.getY() + b.getHeight() * 0.18f);
        p.lineTo (b.getRight(), b.getY() + b.getHeight() * 0.18f);
        p.lineTo (b.getRight(), b.getBottom());
        p.closeSubPath();
        return p;
    }
    inline Path speaker (Rectangle<float> r, bool muted)
    {
        Path p;
        auto s = jmin (r.getWidth(), r.getHeight());
        auto b = r.withSizeKeepingCentre (s, s).reduced (s * 0.24f);
        p.startNewSubPath (b.getX(), b.getY() + b.getHeight() * 0.35f);
        p.lineTo (b.getX() + b.getWidth() * 0.25f, b.getY() + b.getHeight() * 0.35f);
        p.lineTo (b.getX() + b.getWidth() * 0.55f, b.getY());
        p.lineTo (b.getX() + b.getWidth() * 0.55f, b.getBottom());
        p.lineTo (b.getX() + b.getWidth() * 0.25f, b.getY() + b.getHeight() * 0.65f);
        p.lineTo (b.getX(), b.getY() + b.getHeight() * 0.65f);
        p.closeSubPath();
        if (muted)
        {
            p.startNewSubPath (b.getX() + b.getWidth() * 0.72f, b.getY() + b.getHeight() * 0.3f);
            p.lineTo (b.getRight(), b.getY() + b.getHeight() * 0.7f);
            p.startNewSubPath (b.getRight(), b.getY() + b.getHeight() * 0.3f);
            p.lineTo (b.getX() + b.getWidth() * 0.72f, b.getY() + b.getHeight() * 0.7f);
        }
        else
            p.addCentredArc (b.getX() + b.getWidth() * 0.6f, b.getCentreY(), b.getWidth() * 0.3f, b.getHeight() * 0.3f, 0,
                             0.6f, MathConstants<float>::pi - 0.6f, true);
        return p;
    }
}
} // namespace k1t::ui
