#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <array>
#include <atomic>

namespace k1t
{
//==============================================================================
// Un sample en mémoire (toujours stéréo).
struct SampleData
{
    juce::AudioBuffer<float> buf;
    double sr = 48000.0;
    juce::String name;
    mutable std::atomic<int> playingVoices { 0 }; // compteur tenu par le thread audio

    int length() const noexcept { return buf.getNumSamples(); }
    double seconds() const noexcept { return sr > 0 ? length() / sr : 0.0; }
    float peak() const;
};
using SamplePtr = std::shared_ptr<const SampleData>;

SamplePtr loadAudioFile (const juce::File& f, juce::String& error);
bool writeWav (const SampleData& s, const juce::File& dest);
juce::String safeFileName (juce::String s);

//==============================================================================
struct Layer
{
    SamplePtr src;
    juce::String path;     // fichier d'origine (pour la sauvegarde)
    float gainDb  = 0.0f;  // -24 .. +12
    float pitch   = 0.0f;  // demi-tons -12 .. +12
    float nudgeMs = 0.0f;  // -25 .. +25 ms
    float pan     = 0.0f;  // -1 .. 1
    float decay   = 1.0f;  // 0 .. 1 (1 = longueur d'origine)
    float tone    = 0.0f;  // -1 (sombre) .. 1 (brillant)
    bool  mute    = false;
    bool  reverse = false;

    bool active() const { return src != nullptr && ! mute; }
};

static constexpr int kNumLayers = 4;

//==============================================================================
enum FxId { fxTransient, fxEq, fxFilter, fxDrive, fxCrush, fxComp, fxWidth, fxReverb, kNumFx };

struct FxParams
{
    std::array<bool, kNumFx> on {};

    float trAttack = 0.0f, trSustain = 0.0f;                  // -1..1
    // EQ graphique : 0 = low shelf, 1-2 = cloches, 3 = high shelf
    struct EqBand { float freq, gain, q; };
    static constexpr int kEqBands = 4;
    std::array<EqBand, kEqBands> eq { { { 90.0f, 0.0f, 0.71f }, { 450.0f, 0.0f, 1.0f },
                                        { 2800.0f, 0.0f, 1.0f }, { 9000.0f, 0.0f, 0.71f } } };
    static juce::IIRCoefficients eqCoefficients (int band, const EqBand& b, double sr);
    static const char* eqBandName (int band);
    float hpFreq = 20.0f, lpFreq = 20000.0f;
    float drive = 0.3f, driveMix = 1.0f; int driveType = 0;     // 0 soft,1 hard,2 fold,3 tube
    float crushBits = 12.0f, crushRate = 1.0f, crushMix = 1.0f;
    float compThresh = -18.0f, compRatio = 4.0f;
    float width = 1.0f;                                          // 0..2
    float revSize = 0.4f, revMix = 0.15f;

    float outGainDb = 0.0f;
    bool  normalize = true;

    void randomise (juce::Random& r);
    void reset() { *this = FxParams(); }
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& t);
};

const char* fxName (int id);

//==============================================================================
// Décalage aléatoire (stable) appliqué à une couche par le knob SPREAD : -1..1
float spreadFactor (juce::uint32 seed, int layer);

// Mixe les couches (pitch, nudge, decay, tone, pan) puis applique la chaîne d'effets.
SamplePtr renderKit (const std::array<Layer, kNumLayers>& layers, const FxParams& fx,
                     float spreadMs, juce::uint32 spreadSeed, double outSr = 48000.0);

void applyFx (juce::AudioBuffer<float>& buf, double sr, const FxParams& fx);

// Génère des variantes "nouvelles" à partir d'un sample de référence.
std::vector<SamplePtr> generateVariants (const SampleData& ref, int count, float amount,
                                         juce::uint32 seed);

// Variante "808" : note tonale propre, toujours accordée en C (C5 dans FL),
// varie la longueur, la saturation, le punch et le sub — jamais le pitch.
float detect808Pitch (const SampleData& ref);   // Hz (0 si introuvable)
juce::String noteName (float hz);

std::vector<SamplePtr> generate808Variants (const SampleData& ref, int count, float amount,
                                            juce::uint32 seed);

// Utilitaires
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double ratio); // ratio = pas de lecture
void normalisePeak (juce::AudioBuffer<float>& b, float targetDb);
void trimSilence (juce::AudioBuffer<float>& b, double sr, float thresholdDb = -72.0f);
} // namespace k1t
