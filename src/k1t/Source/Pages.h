#pragma once
#include "Widgets.h"
#include "Flp.h"
#include <set>

class K1TProcessor;

namespace k1t::ui
{
struct Ctx
{
    K1TProcessor& p;
    Toast& toast;
    Particles& particles;
    Component& root;
    void burstAt (Component& c, Point<float> local, int count = 36)
    {
        particles.burst (root.getLocalPoint (&c, local), count);
    }
};

bool isAudioFile (const String& path);

//==============================================================================
class LayerCard : public Component, public FileDragAndDropTarget, private Anim
{
public:
    LayerCard (Ctx&, int index, std::function<void()> changed);
    void refresh();
    void resized() override;
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;

    bool isInterestedInFileDrag (const StringArray&) override;
    void fileDragEnter (const StringArray&, int, int) override { dragOver = true; }
    void fileDragExit (const StringArray&) override { dragOver = false; }
    void filesDropped (const StringArray&, int, int) override;
    void browse();

private:
    void tick (float dt) override;
    Ctx& ctx;
    int index;
    std::function<void()> changed;
    WaveView wave;
    Knob vol, pitch, nudge, pan, decay, tone;
    Pill play { "", Pill::subtle, Pill::play }, mute { "", Pill::subtle, Pill::speaker },
         rev { "", Pill::subtle, Pill::reverse }, remove { "", Pill::subtle, Pill::cross };
    std::unique_ptr<FileChooser> chooser;
    bool dragOver = false;
    float hover = 0, drop = 0, dash = 0, appear = 1;
};

//==============================================================================
class TimingLane : public Component, private Anim
{
public:
    TimingLane (Ctx&, std::function<void()> changed);
    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override { dragging = -1; }
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override { hovered = -1; }
    void mouseDoubleClick (const MouseEvent&) override;

private:
    void tick (float dt) override;
    Rectangle<float> laneArea() const;
    float msToX (float ms) const;
    float xToMs (float x) const;
    float laneY (int i) const;
    int dotAt (Point<float>) const;
    Ctx& ctx;
    std::function<void()> changed;
    std::array<float, 4> shownNudge {}, shownFinal {};
    int dragging = -1, hovered = -1;
};

//==============================================================================
// EQ graphique : on attrape les points à la main.
class EqGraph : public Component, public SettableTooltipClient, private Anim
{
public:
    EqGraph (Ctx&, std::function<void()> changed, bool large);
    void setSpectrum (const SampleData* s);
    void paint (Graphics&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

private:
    void tick (float dt) override;
    Rectangle<float> plot() const;
    float fToX (float f) const;
    float xToF (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    Point<float> nodePos (int i) const;
    int nodeAt (Point<float>) const;
    float responseDb (float f) const;
    void edited();

    Ctx& ctx;
    std::function<void()> changed;
    bool large;
    int hovered = -1, dragging = -1;
    Point<float> lastPos;
    std::array<float, 4> nodeHover {};
    std::vector<float> spectrum;   // 0..1, en log-fréquence
    float range = 18.0f;
};

//==============================================================================
class FxModule : public Component, private Anim
{
public:
    FxModule (Ctx&, int fxId, std::function<void()> changed);
    Knob& addKnob (const String& label, double mn, double mx, double def,
                   std::function<float()> get, std::function<void (float)> set,
                   std::function<String (double)> fmt, double skewMid = 0);
    void addSegment (StringArray items, std::function<int()> get, std::function<void (int)> set);
    void setCustom (Component* c, int height) { custom = c; customH = height; addAndMakeVisible (c); }
    void switchOn();
    Pill expand { "", Pill::subtle, Pill::expand };
    bool hasExpand = false;
    void refresh();
    int preferredHeight() const;
    void resized() override;
    void paint (Graphics&) override;

private:
    void tick (float dt) override;
    Ctx& ctx;
    int fxId;
    std::function<void()> changed;
    Switch sw;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::function<float()>> getters;
    std::unique_ptr<Segmented> seg;
    std::function<int()> segGet;
    Component* custom = nullptr;
    int customH = 0;
    float dimShown = 1.0f;
};

class FxPanel : public Component
{
public:
    FxPanel (Ctx&, std::function<void()> changed);
    void refresh();
    void kitChanged (const SampleData* s) { eq.setSpectrum (s); }
    void repaintEq() { eq.repaint(); }
    std::function<void()> onExpandEq;
    void resized() override;
    void paint (Graphics&) override;

private:
    Ctx& ctx;
    std::function<void()> changed;
    Pill enrich { "ENRICH", Pill::primary, Pill::sparkle }, reset { "RESET", Pill::subtle };
    Viewport view;
    Component list;
    std::vector<std::unique_ptr<FxModule>> modules;
    FxModule* eqModule = nullptr;
    EqGraph eq;
    Knob out { "OUT", -18, 6, 0 };
    Switch normalize;
};

//==============================================================================
class DragPill : public Pill
{
public:
    using Pill::Pill;
    std::function<void (Component*)> onDragOut;
    void mouseDrag (const MouseEvent& e) override
    {
        if (! started && e.getDistanceFromDragStart() > 5 && onDragOut) { started = true; onDragOut (this); }
    }
    void mouseUp (const MouseEvent& e) override { started = false; Pill::mouseUp (e); }

private:
    bool started = false;
};

//==============================================================================
class EqOverlay : public Component, private Anim
{
public:
    EqOverlay (Ctx&, std::function<void()> changed);
    void open();
    void close();
    bool isOpen() const { return target > 0.5f; }
    void setSpectrum (const SampleData* s) { graph.setSpectrum (s); }
    void repaintGraph() { graph.repaint(); }
    void resized() override;
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;

private:
    void tick (float dt) override;
    Ctx& ctx;
    EqGraph graph;
    Pill closeBtn { "", Pill::subtle, Pill::cross }, resetBtn { "RESET EQ", Pill::ghost };
    Switch power;
    float shown = 0, target = 0;
    Rectangle<int> card;
};

class MixPage : public Component
{
public:
    explicit MixPage (Ctx&);
    void refresh();
    void kitChanged();
    void resized() override;
    void paint (Graphics&) override;
    void playKit();
    bool goBack();
    void addFiles (const StringArray& files, int startLayer);

private:
    void changed();
    Ctx& ctx;
    std::vector<std::unique_ptr<LayerCard>> cards;
    WaveView result;
    Pill playBtn { "PLAY", Pill::ghost, Pill::play };
    Knob spread { "SPREAD", 0, 15, 0, col::magenta };
    Segmented presets { { "TIGHT", "DOUBLE", "FLAM" }, false };
    Pill shuffle { "", Pill::ghost, Pill::dice };
    TimingLane timing;
    DragPill dragOut { "GLISSE DANS FL", Pill::primary, Pill::arrowDown };
    Pill save { "SAVE WAV", Pill::ghost, Pill::folder };
    Pill toKit { "+ KIT", Pill::ghost, Pill::plus }, openKit { "", Pill::subtle, Pill::folder };
    Switch choke;
    FxPanel fx;
    EqOverlay eqBig;
    std::unique_ptr<FileChooser> chooser;
    Rectangle<int> resultCard, timingCard, layersArea, exportRow;
};

//==============================================================================
class VariantCard : public Component, private Anim
{
public:
    VariantCard (Ctx&, int index);
    void setSample (SamplePtr s, float delay);
    void resized() override;
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    std::function<void (SamplePtr)> onAddLayer, onAddKit, onExport;

private:
    void tick (float dt) override;
    Ctx& ctx;
    int index;
    SamplePtr sample;
    WaveView wave;
    Pill add { "LAYER", Pill::subtle, Pill::plus }, kit { "KIT", Pill::subtle, Pill::plus }, exp { "", Pill::subtle, Pill::arrowDown };
    float appear = 0, delay = 0, hover = 0, playing = 0;
    bool dragged = false;
};

class GenPage : public Component, public FileDragAndDropTarget, private Anim
{
public:
    explicit GenPage (Ctx&);
    void refresh (bool animate);
    void resized() override;
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;
    bool isInterestedInFileDrag (const StringArray&) override;
    void fileDragEnter (const StringArray&, int, int) override { dragOver = true; }
    void fileDragExit (const StringArray&) override { dragOver = false; }
    void filesDropped (const StringArray&, int, int) override;
    void playReference();
    bool goBack();
    std::function<void()> onLayersChanged;

private:
    void tick (float dt) override;
    void loadRef (const File&);
    void exportOne (SamplePtr s);
    void runGenerate();
    Ctx& ctx;
    WaveView ref;
    Knob amount { "VARIATION", 0, 1, 0.5, col::magenta };
    Segmented mode { { "TOUT", "808" }, false };
    Pill generate { "GENERATE", Pill::primary, Pill::sparkle };
    Pill exportAll { "EXPORT ALL", Pill::ghost, Pill::folder };
    Pill refPlay { "", Pill::subtle, Pill::play };
    std::vector<std::unique_ptr<VariantCard>> cards;
    std::unique_ptr<FileChooser> chooser;
    Rectangle<int> refCard, gridArea, refWaveArea;
    bool dragOver = false;
    float drop = 0, dash = 0;
};

//==============================================================================
class FlpPage : public Component, public FileDragAndDropTarget, private Anim
{
public:
    explicit FlpPage (Ctx&);
    void refresh();
    void resized() override;
    void paint (Graphics&) override;
    void mouseUp (const MouseEvent&) override;
    bool isInterestedInFileDrag (const StringArray&) override;
    void fileDragEnter (const StringArray&, int, int) override { dragOver = true; }
    void fileDragExit (const StringArray&) override { dragOver = false; }
    void filesDropped (const StringArray&, int, int) override;
    bool goBack();
    std::function<void()> onLayersChanged;

private:
    void tick (float dt) override;
    void load (const File&);
    void browse();
    void extract();
    void togglePattern (const FlpPattern* p);
    void updateExtractLabel();

    struct Model : public ListBoxModel
    {
        FlpPage& page;
        bool samples;
        Model (FlpPage& p, bool s) : page (p), samples (s) {}
        int getNumRows() override;
        void paintListBoxItem (int row, Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const MouseEvent&) override;
    };
    const FlpProject* shownProject = nullptr;
    std::vector<const FlpPattern*> patRows;
    std::vector<const FlpChannel*> smpRows;
    std::set<int> selPatterns, selChannels;
    SamplePtr loadCached (const File&);
    std::map<String, SamplePtr> cache;
    std::map<int, PatternRender> renders;
    int playingPattern = -1;
    const SampleData* playingAudio = nullptr;

    Ctx& ctx;
    Model midiModel { *this, false }, smpModel { *this, true };
    ListBox midiList, smpList;
    Pill extractBtn { "EXTRAIRE MIDI + SAMPLES", Pill::primary, Pill::arrowDown };
    Pill searchBtn { String::fromUTF8 ("CHERCHER LES SAMPLES…"), Pill::ghost, Pill::folder };
    Pill otherBtn { "AUTRE .FLP", Pill::subtle };
    Pill allPat { "TOUT", Pill::subtle }, allSmp { "TOUT", Pill::subtle };
    std::unique_ptr<FileChooser> chooser;
    Rectangle<int> dropZone, infoArea, midiCard, smpCard;
    bool dragOver = false;
    float drop = 0, dash = 0, appear = 1;
};
} // namespace k1t::ui
