#pragma once
#include "PluginProcessor.h"
#include "Pages.h"
#include "Updater.h"

//==============================================================================
class UpdatePanel : public juce::Component, private k1t::ui::Anim, private juce::Timer
{
public:
    explicit UpdatePanel (k1t::ui::Ctx&);
    ~UpdatePanel() override;
    void checkNow (bool userAsked);
    void offer (const k1t::update::Candidate& c, bool fromUser);
    bool hasPending() const { return candidate.isValid() && mode == available && ! visibleTarget; }
    std::function<void()> onStateChanged;
    void show();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Mode { none, available, running, done, failed };
    void tick (float dt) override;
    void timerCallback() override;
    void setMode (Mode m);
    void hide();

    k1t::ui::Ctx& ctx;
    Mode mode = none;
    k1t::update::Candidate candidate;
    k1t::update::Progress progress;
    k1t::ui::Pill primary { juce::String::fromUTF8 ("METTRE À JOUR"), k1t::ui::Pill::primary, k1t::ui::Pill::refresh },
                  secondary { "PLUS TARD", k1t::ui::Pill::subtle };
    k1t::ui::Spring y;
    float shownFraction = 0, pulse = 0;
    bool visibleTarget = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

class K1TLookAndFeel : public juce::LookAndFeel_V4
{
public:
    K1TLookAndFeel();
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
};

class K1TEditor : public juce::AudioProcessorEditor,
                  public juce::FileDragAndDropTarget,
                  private juce::ChangeListener,
                  private k1t::ui::Anim
{
public:
    explicit K1TEditor (K1TProcessor&);
    ~K1TEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void tick (float dt) override;
    void showTab (int tab, bool animate);
    void renderBlobs();
    void drawLogo (juce::Graphics&, juce::Rectangle<float>);

    K1TProcessor& proc;
    K1TLookAndFeel lnf;

    k1t::ui::Toast toast;
    k1t::ui::Particles particles;
    k1t::ui::Ctx ctx { proc, toast, particles, *this };

    k1t::ui::Segmented tabs { { "MIX", "GENERATE", "FLP" }, true };
    k1t::ui::MixPage mix { ctx };
    k1t::ui::GenPage gen { ctx };
    k1t::ui::FlpPage flp { ctx };
    juce::TooltipWindow tooltips { this, 500 };
    UpdatePanel updates { ctx };
    k1t::ui::Pill updPill { juce::String::fromUTF8 ("MISE À JOUR"), k1t::ui::Pill::primary, k1t::ui::Pill::refresh };
    juce::Rectangle<int> versionArea;
    k1t::ui::Pill backBtn { "RETOUR", k1t::ui::Pill::ghost, k1t::ui::Pill::undo };
    void goBack();
    bool canGoBack() const;
    std::unique_ptr<juce::VBlankAttachment> vblank;

    std::array<juce::Component*, 3> pages {};
    int current = 0, previous = -1;
    float transition = 1.0f;

    juce::Image blobs;
    float blobTime = 0, blobAccum = 0, meter = 0, logoGlow = 0;
    juce::Rectangle<int> content, meterArea, logoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (K1TEditor)
};
