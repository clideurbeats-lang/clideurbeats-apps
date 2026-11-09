#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Flp.h"

class K1TProcessor : public juce::AudioProcessor,
                     public juce::ChangeBroadcaster,
                     private juce::Timer
{
public:
    K1TProcessor();
    ~K1TProcessor() override;

    //== AudioProcessor ==========================================================
    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "K1T"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "K1T"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== État du kit (thread message) =============================================
    std::array<k1t::Layer, k1t::kNumLayers> layers;
    k1t::FxParams fx;
    float spreadMs = 0.0f;
    juce::uint32 spreadSeed = 1;
    bool choke = false;

    juce::String loadLayer (int index, const juce::File& f);
    void setLayer (int index, k1t::SamplePtr s, const juce::String& path);
    void clearLayer (int index);
    int firstEmptyLayer() const;

    void requestRender() { renderDirty = true; if (! restoringMix) { mixPending = true; mixChangedAt = juce::Time::getMillisecondCounterHiRes(); } }

    //== Retour en arrière =========================================================
    bool canUndoMix() const { return mixPending || ! mixUndo.empty(); }
    bool undoMix();
    bool canUndoGenerate() const { return ! genHistory.empty(); }
    void flushMixHistory() { if (mixPending) commitMix(); }
    bool undoGenerate();
    void renderNow();
    k1t::SamplePtr getKit() const { return kit; }

    //== Générateur ===============================================================
    k1t::SamplePtr reference;
    juce::String referencePath;
    float genAmount = 0.5f;
    int genMode = 0;                              // 0 = TOUT, 1 = 808
    juce::uint32 genSeed = 0;
    std::vector<k1t::SamplePtr> variants;
    juce::String loadReference (const juce::File& f);
    void generate (bool newSeed = true);
    struct GenBatch { std::vector<k1t::SamplePtr> variants; juce::uint32 seed; int mode; k1t::SamplePtr ref; juce::String refPath; float amount; };
    std::vector<GenBatch> genHistory;

    //== FLP ======================================================================
    std::unique_ptr<k1t::FlpProject> flp;
    juce::File flpSearchDir;

    //== Drum kit (dossier rangé par catégories) ===================================
    juce::String kitName;
    int kitCount = 0;
    juce::File kitFolder();                      // ~/Music/K1T/Kits/<kitName>
    juce::File addToKit (const k1t::SampleData& s, const juce::String& name, const juce::String& categoryHint = {});

    //== UI ========================================================================
    int currentTab = 0;
    static juce::File appFolder();               // ~/Music/K1T
    juce::File exportForDrag (const k1t::SampleData& s, const juce::String& name);

    //== Lecture ==================================================================
    void preview (k1t::SamplePtr s);
    void previewLoop (k1t::SamplePtr s);           // lecture en boucle (patterns FLP)
    const k1t::SampleData* loopingSample() const { return loopRaw.load(); }
    void stopAll() { stopRequest = true; }
    // Pour la tête de lecture de l'UI
    const k1t::SampleData* playingSample() const { return lastPlayed.load(); }
    float playingPosition() const { return lastPos.load(); }
    float outputLevel() const { return outLevel.load(); }

private:
    void timerCallback() override;
    void keepAlive (k1t::SamplePtr s);
    void renderVoices (juce::AudioBuffer<float>& out, int start, int num);
    void startVoice (const k1t::SampleData* s, int note, float velocity, bool fromMidi, bool loop = false);

    struct MixState
    {
        std::array<k1t::Layer, k1t::kNumLayers> layers;
        k1t::FxParams fx;
        float spreadMs = 0; juce::uint32 spreadSeed = 1; bool choke = false;
    };
    MixState captureMix() const;
    void applyMix (const MixState&);
    static juce::String signature (const MixState&);
    void commitMix();
    std::vector<MixState> mixUndo;
    MixState mixCommitted;
    bool mixPending = false, restoringMix = false;
    double mixChangedAt = 0;

    k1t::SamplePtr kit;
    bool renderDirty = false;

    struct Graveyard { k1t::SamplePtr s; double time; };
    k1t::SamplePtr previewHeld;
    std::vector<Graveyard> graveyard;

    struct Voice
    {
        const k1t::SampleData* s = nullptr;
        double pos = 0, step = 1;
        float gain = 1, fade = 1, fadeStep = 0;
        bool active = false, isKit = false, loop = false;
        juce::uint64 age = 0;
    };
    std::array<Voice, 24> voices;
    juce::uint64 voiceCounter = 0;
    double hostSr = 44100.0;

    std::atomic<const k1t::SampleData*> kitRaw { nullptr };
    std::atomic<const k1t::SampleData*> pendingPreview { nullptr };
    std::atomic<bool> pendingLoop { false };
    std::atomic<const k1t::SampleData*> loopRaw { nullptr };
    std::atomic<bool> stopRequest { false };
    std::atomic<bool> chokeRaw { false };
    std::atomic<const k1t::SampleData*> lastPlayed { nullptr };
    std::atomic<float> lastPos { -1.0f };
    std::atomic<float> outLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (K1TProcessor)
};
