#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <set>
#include <functional>
#include "Engine.h"

namespace k1t
{
// Lecture des projets FL Studio (.flp) : patterns, notes, channels et samples utilisés.
struct FlpNote
{
    juce::uint32 position = 0, length = 0;
    int channel = 0, key = 60, velocity = 100;
};

struct FlpChannel
{
    int id = 0;
    int type = -1;              // 0 sampler, 2 plugin, 4 audio clip...
    juce::String name, plugin, samplePath;
    juce::File resolved;        // fichier trouvé sur le disque (si existe)

    juce::String displayName() const;
};

struct FlpPattern
{
    int id = 0;
    juce::String name;
    std::vector<FlpNote> notes;
};

struct FlpProject
{
    juce::File file;
    juce::String title, version;
    int ppq = 96;
    double tempo = 140.0;
    std::map<int, FlpChannel> channels;
    std::map<int, FlpPattern> patterns;

    int numNotes() const;
    int numPatternsWithNotes() const;
    int numSamples() const;
    int numSamplesFound() const;
};

bool parseFlp (const juce::File& f, FlpProject& out, juce::String& error);

// Cherche les samples manquants (dossier du .flp, dossiers FL Studio, dossier optionnel).
void resolveSamples (FlpProject& p, const juce::File& extraSearchDir = {});

struct FlpExportResult { int midiFiles = 0, samplesCopied = 0, samplesMissing = 0; juce::File folder; };
// patterns / channels : nullptr = tout exporter, sinon seulement les ids cochés
FlpExportResult exportFlp (const FlpProject& p, const juce::File& destRoot,
                           const std::set<int>* patterns = nullptr, const std::set<int>* channels = nullptr);

// Rend un pattern en audio avec les vrais samples du projet (pour l'écouter en boucle).
struct PatternRender { SamplePtr audio; int channelsPlayed = 0, channelsSilent = 0; double seconds = 0; };
PatternRender renderPattern (const FlpProject& p, const FlpPattern& pat,
                             const std::function<SamplePtr (const juce::File&)>& load, double sr = 44100.0);
} // namespace k1t
