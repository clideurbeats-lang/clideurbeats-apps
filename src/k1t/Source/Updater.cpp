#include "Updater.h"

namespace k1t::update
{
using namespace juce;

String currentVersion()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "3.1.0";
   #endif
}

int compareVersions (const String& a, const String& b)
{
    auto pa = StringArray::fromTokens (a.retainCharacters ("0123456789."), ".", "");
    auto pb = StringArray::fromTokens (b.retainCharacters ("0123456789."), ".", "");
    for (int i = 0; i < jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

String readScriptVersion (const File& script)
{
    FileInputStream in (script);
    if (! in.openedOk()) return {};
    MemoryBlock mb;
    in.readIntoMemoryBlock (mb, 6000);
    const auto head = mb.toString();
    if (! head.contains ("K1T")) return {};
    const int i = head.indexOf ("VERSION=\"");
    if (i < 0) return {};
    return head.substring (i + 9).upToFirstOccurrenceOf ("\"", false, false).trim();
}

File updatesFolder()
{
    auto f = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("K1T").getChildFile ("Updates");
    f.createDirectory();
    return f;
}

File logFile() { return updatesFolder().getChildFile ("update.log"); }

File buildLogFile()
{
    return File::getSpecialLocation (File::userHomeDirectory).getChildFile ("K1T-build").getChildFile ("build.log");
}

Candidate findUpdate()
{
    Candidate best;
    const auto home = File::getSpecialLocation (File::userHomeDirectory);
    const File dirs[] = { home.getChildFile ("Downloads"), home.getChildFile ("Desktop"), updatesFolder() };
    const auto cur = currentVersion();

    for (auto& d : dirs)
    {
        if (! d.isDirectory()) continue;
        for (auto& e : RangedDirectoryIterator (d, false, "installer_k1t*.sh", File::findFiles))
        {
            const auto v = readScriptVersion (e.getFile());
            if (v.isEmpty() || compareVersions (v, cur) <= 0) continue;
            if (! best.isValid() || compareVersions (v, best.version) > 0)
                best = { e.getFile(), v };
        }
    }
    return best;
}

String manifestUrl()
{
    return "https://raw.githubusercontent.com/clideurbeats-lang/clideurbeats-apps/main/updates/k1t.json";
}

Candidate checkOnline()
{
    Candidate res;
    auto opts = URL::InputStreamOptions (URL::ParameterHandling::inAddress).withConnectionTimeoutMs (6000).withNumRedirectsToFollow (5);
    std::unique_ptr<InputStream> in (URL (manifestUrl()).createInputStream (opts));
    if (in == nullptr) return res;
    const auto json = JSON::parse (in->readEntireStreamAsString());
    const String version = json.getProperty ("version", var()).toString();
    const String url = json.getProperty ("installer", var()).toString();
    const String pkg = json.getProperty ("pkg", var()).toString();
    if (version.isEmpty() || compareVersions (version, currentVersion()) <= 0) return res;

    // un .pkg prêt à installer : marche sur tous les Mac, sans outils de développement
    if (pkg.isNotEmpty())
    {
        res.version = version;
        res.pkgUrl = pkg;
        res.fromWeb = true;
        return res;
    }
    if (url.isEmpty()) return res;

    const auto dest = updatesFolder().getChildFile ("installer_k1t_v" + version + ".sh");
    if (readScriptVersion (dest) != version)
    {
        std::unique_ptr<InputStream> dl (URL (url).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                                        .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (5)));
        if (dl == nullptr) return res;
        MemoryBlock mb;
        dl->readIntoMemoryBlock (mb);
        if (mb.getSize() < 1000 || ! mb.toString().startsWith ("#!/bin/bash")) return res;
        const auto tmp = dest.getSiblingFile (dest.getFileName() + ".part");
        if (! tmp.replaceWithData (mb.getData(), mb.getSize())) return res;
        if (readScriptVersion (tmp) != version) { tmp.deleteFile(); return res; }
        dest.deleteFile();
        tmp.moveFileTo (dest);
    }
    res.script = dest;
    res.version = version;
    res.fromWeb = true;
    return res;
}

static void installPkg (String version, String pkgUrl)
{
    const auto log = logFile();
    auto write = [log] (const String& line) { log.appendText (line + "\n", false, false, "\n"); };
    const auto dest = updatesFolder().getChildFile ("K1T_v" + version + ".pkg");
    const auto tmp = dest.getSiblingFile (dest.getFileName() + ".part");

    std::unique_ptr<InputStream> in (URL (pkgUrl).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                                        .withConnectionTimeoutMs (20000).withNumRedirectsToFollow (8)));
    if (in == nullptr) { write (String::fromUTF8 ("K1T_ERROR Téléchargement impossible (Internet ?)")); return; }
    tmp.deleteFile();
    {
        FileOutputStream out (tmp);
        if (! out.openedOk()) { write (String::fromUTF8 ("K1T_ERROR Impossible d'écrire dans ") + updatesFolder().getFullPathName()); return; }
        const int64 total = in->getTotalLength();
        HeapBlock<char> buf (1 << 16);
        int64 done = 0;
        int lastPct = -1;
        for (;;)
        {
            const int n = in->read (buf, 1 << 16);
            if (n <= 0) break;
            out.write (buf, (size_t) n);
            done += n;
            const int pct = total > 0 ? (int) (100 * done / total) : -1;
            if (pct >= 0 && pct / 10 != lastPct / 10) { lastPct = pct; write (String::fromUTF8 ("K1T_PROGRESS 1 2 Téléchargement ") + String (pct) + " %"); }
        }
        out.flush();
    }
    if (tmp.getSize() < 100000) { tmp.deleteFile(); write (String::fromUTF8 ("K1T_ERROR Le fichier téléchargé est incomplet, réessaie")); return; }
    dest.deleteFile();
    tmp.moveFileTo (dest);

    write (String::fromUTF8 ("K1T_PROGRESS 2 2 Ouverture de l'installeur"));
    ChildProcess p;
    if (! p.start (StringArray { "/usr/bin/open", dest.getFullPathName() }, 0)) { write (String::fromUTF8 ("K1T_ERROR Impossible d'ouvrir l'installeur")); return; }
    p.waitForProcessToFinish (10000);
    write ("K1T_DONE " + version + " pkg");
}

bool launch (const Candidate& c, String& error)
{
    if (! c.isValid()) { error = String::fromUTF8 ("Fichier de mise à jour introuvable"); return false; }

    if (c.pkgUrl.isNotEmpty())
    {
        logFile().replaceWithText (String::fromUTF8 ("K1T_PROGRESS 1 2 Téléchargement\n"), false, false, "\n");
        Thread::launch ([v = c.version, u = c.pkgUrl] { installPkg (v, u); });
        return true;
    }

    // copie locale : la mise à jour continue même si le fichier d'origine bouge
    const auto local = updatesFolder().getChildFile ("installer_k1t_v" + c.version + ".sh");
    if (c.script != local)
    {
        local.deleteFile();
        if (! c.script.copyFileTo (local)) { error = String::fromUTF8 ("Impossible de copier la mise à jour"); return false; }
    }
    logFile().replaceWithText (String::fromUTF8 ("K1T_PROGRESS 0 6 Démarrage\n"), false, false, "\n");

    // lancé détaché (nohup + &) : fermer FL ou le plugin n'interrompt pas la compilation
    const String cmd = "nohup /bin/bash " + local.getFullPathName().quoted ('\'')
                     + " --plugin > " + logFile().getFullPathName().quoted ('\'') + " 2>&1 &";
    ChildProcess p;
    if (! p.start (StringArray { "/bin/bash", "-c", cmd }, 0)) { error = String::fromUTF8 ("Impossible de lancer la mise à jour"); return false; }
    p.waitForProcessToFinish (5000);
    return true;
}

float Progress::fraction() const
{
    if (state == done) return 1.0f;
    float f = (float) jmax (0, step - 1) / (float) jmax (1, steps);
    if (buildPercent >= 0.0f) f += buildPercent / (float) jmax (1, steps);
    return jlimit (0.0f, 1.0f, f);
}

Progress readProgress()
{
    Progress p;
    const auto f = logFile();
    if (! f.existsAsFile()) return p;
    StringArray lines;
    f.readLines (lines);
    for (auto& l : lines)
    {
        if (l.startsWith ("K1T_PROGRESS"))
        {
            auto t = StringArray::fromTokens (l, " ", "");
            p.state = Progress::running;
            p.step = t[1].getIntValue();
            p.steps = jmax (1, t[2].getIntValue());
            t.removeRange (0, 3);
            p.label = t.joinIntoString (" ");
            p.buildPercent = -1.0f;
        }
        else if (l.startsWith ("K1T_DONE"))
        {
            p.state = Progress::done;
            p.label = l.fromFirstOccurrenceOf (" ", false, false).trim();
            p.viaPkg = p.label.endsWith (" pkg");
            if (p.viaPkg) p.label = p.label.dropLastCharacters (4).trim();
        }
        else if (l.startsWith ("K1T_ERROR")) { p.state = Progress::failed; p.error = l.fromFirstOccurrenceOf (" ", false, false); }
    }

    // pourcentage de compilation (sortie de make : "[ 42%] Building …")
    if (p.state == Progress::running && p.label.containsIgnoreCase ("compil"))
    {
        const auto bl = buildLogFile();
        if (bl.existsAsFile() && bl.getLastModificationTime() > f.getCreationTime() - RelativeTime::minutes (1))
        {
            FileInputStream in (bl);
            if (in.openedOk())
            {
                const int64 size = in.getTotalLength();
                in.setPosition (jmax ((int64) 0, size - 4000));
                const auto tail = in.readEntireStreamAsString();
                const int i = tail.lastIndexOf ("%]");
                if (i > 3)
                {
                    const auto pct = tail.substring (jmax (0, i - 4), i).retainCharacters ("0123456789");
                    if (pct.isNotEmpty()) p.buildPercent = jlimit (0.0f, 1.0f, pct.getIntValue() / 100.0f);
                }
            }
        }
    }
    return p;
}
} // namespace k1t::update
