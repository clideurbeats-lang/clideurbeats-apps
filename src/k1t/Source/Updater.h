#pragma once
#include <juce_core/juce_core.h>

// Mise à jour sans Terminal :
//  1. on dépose (ou télécharge) un nouveau installer_k1t_vX.sh dans Téléchargements / Bureau
//  2. K1T le repère, propose « Mettre à jour »
//  3. le script est lancé en arrière-plan (détaché du plugin), il recompile et réinstalle K1T
//  4. K1T suit la progression dans un fichier log et dit quand relancer.
namespace k1t::update
{
juce::String currentVersion();
int compareVersions (const juce::String& a, const juce::String& b); // <0, 0, >0

struct Candidate
{
    juce::File script;
    juce::String version;
    bool fromWeb = false;
    juce::String pkgUrl;   // installeur .pkg tout prêt (pas besoin de compiler : Mac des potes)
    bool isValid() const { return version.isNotEmpty() && (pkgUrl.isNotEmpty() || script.existsAsFile()); }
};

juce::String readScriptVersion (const juce::File& script);   // VERSION="x.y" dans l'en-tête
Candidate findUpdate();                                        // le plus récent > version actuelle (Téléchargements, Bureau…)
Candidate checkOnline();                                       // site @clideurbeats (GitHub) : télécharge la nouvelle version
juce::String manifestUrl();
juce::File updatesFolder();                                    // ~/Music/K1T/Updates
juce::File logFile();
juce::File buildLogFile();                                     // ~/K1T-build/build.log

bool launch (const Candidate& c, juce::String& error);

struct Progress
{
    enum State { idle, running, done, failed } state = idle;
    int step = 0, steps = 6;
    juce::String label, error;
    float buildPercent = -1.0f;   // pendant la compilation
    bool viaPkg = false;          // installé via le .pkg (l'installeur macOS s'est ouvert)
    float fraction() const;
};
Progress readProgress();
} // namespace k1t::update
