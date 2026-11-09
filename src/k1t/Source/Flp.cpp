#include "Flp.h"
#include "Engine.h"

namespace k1t
{
using namespace juce;

String FlpChannel::displayName() const
{
    if (name.isNotEmpty()) return name;
    if (samplePath.isNotEmpty())
        return File::createFileWithoutCheckingPath (samplePath.replaceCharacter ('\\', '/')
                                                        .fromLastOccurrenceOf ("/", false, false)).getFileNameWithoutExtension();
    if (plugin.isNotEmpty()) return plugin;
    return "Channel " + String (id + 1);
}

int FlpProject::numNotes() const
{
    int n = 0;
    for (auto& [id, p] : patterns) n += (int) p.notes.size();
    return n;
}
int FlpProject::numPatternsWithNotes() const
{
    int n = 0;
    for (auto& [id, p] : patterns) n += p.notes.empty() ? 0 : 1;
    return n;
}
int FlpProject::numSamples() const
{
    int n = 0;
    for (auto& [id, c] : channels) n += c.samplePath.isNotEmpty() ? 1 : 0;
    return n;
}
int FlpProject::numSamplesFound() const
{
    int n = 0;
    for (auto& [id, c] : channels) n += (c.samplePath.isNotEmpty() && c.resolved.existsAsFile()) ? 1 : 0;
    return n;
}

//==============================================================================
namespace
{
    enum : int
    {
        evChanType = 21,
        evChanNew = 64, evPatNew = 65, evTempoCoarse = 66, evSlotIndex = 98,
        evTempoFine = 156,
        evChanName0 = 192, evPatName = 193, evTitle = 194, evSamplePath = 196,
        evVersion = 199, evDefPlugin = 201, evPluginName = 203,
        evPatNotes = 224, evMixerParams = 225, evChanName2 = 231, evInsertRouting = 235, evInsertFlags = 236
    };

    String decodeText (const uint8* d, size_t len, bool utf16)
    {
        if (len == 0) return {};
        bool looks16 = utf16;
        if (! utf16 && len >= 4 && (len % 2) == 0 && d[1] == 0 && d[3] == 0) looks16 = true;

        if (looks16)
        {
            std::vector<CharPointer_UTF16::CharType> w (len / 2 + 1, 0);
            for (size_t i = 0; i + 1 < len; i += 2)
                w[i / 2] = (CharPointer_UTF16::CharType) (d[i] | (d[i + 1] << 8));
            return String (CharPointer_UTF16 (w.data())).trim();
        }
        return String::fromUTF8 ((const char*) d, (int) strnlen ((const char*) d, len)).trim();
    }

    uint32 rd32 (const uint8* p) { return (uint32) p[0] | ((uint32) p[1] << 8) | ((uint32) p[2] << 16) | ((uint32) p[3] << 24); }
    uint16 rd16 (const uint8* p) { return (uint16) (p[0] | (p[1] << 8)); }
}

bool parseFlp (const File& f, FlpProject& out, String& error)
{
    MemoryBlock mb;
    if (! f.loadFileAsData (mb) || mb.getSize() < 22) { error = "Impossible de lire le fichier."; return false; }

    const auto* d = (const uint8*) mb.getData();
    const size_t size = mb.getSize();
    if (memcmp (d, "FLhd", 4) != 0) { error = "Ce n'est pas un projet FL Studio (.flp)."; return false; }

    out = FlpProject();
    out.file = f;
    out.title = f.getFileNameWithoutExtension();

    const uint32 hdrLen = rd32 (d + 4);
    if (hdrLen >= 6) out.ppq = jmax (1, (int) rd16 (d + 12));
    size_t pos = 8 + hdrLen;
    if (pos + 8 > size || memcmp (d + pos, "FLdt", 4) != 0) { error = "Fichier .flp corrompu (bloc FLdt absent)."; return false; }
    pos += 8;

    bool utf16 = false;
    int curChan = -1, curPat = -1;
    bool sawFineTempo = false;

    while (pos < size)
    {
        const int id = d[pos++];
        size_t len = 0;
        uint32 value = 0;
        const uint8* data = nullptr;

        if (id < 64)        { if (pos + 1 > size) break; value = d[pos]; pos += 1; }
        else if (id < 128)  { if (pos + 2 > size) break; value = rd16 (d + pos); pos += 2; }
        else if (id < 192)  { if (pos + 4 > size) break; value = rd32 (d + pos); pos += 4; }
        else
        {
            int shift = 0;
            while (pos < size)
            {
                const uint8 b = d[pos++];
                len |= (size_t) (b & 0x7f) << shift;
                shift += 7;
                if ((b & 0x80) == 0 || shift > 28) break;
            }
            if (pos + len > size) break;
            data = d + pos;
            pos += len;
        }

        switch (id)
        {
            case evVersion:
            {
                out.version = decodeText (data, len, false);
                const auto parts = StringArray::fromTokens (out.version, ".", "");
                const int maj = parts[0].getIntValue(), mn = parts[1].getIntValue();
                utf16 = maj > 11 || (maj == 11 && mn >= 5);
                break;
            }
            case evTitle:       { auto t = decodeText (data, len, utf16); if (t.isNotEmpty()) out.title = t; break; }
            case evTempoFine:   out.tempo = value / 1000.0; sawFineTempo = true; break;
            case evTempoCoarse: if (! sawFineTempo && value > 0) out.tempo = value; break;

            case evChanNew:
                curChan = (int) value; curPat = -1;
                out.channels[curChan].id = curChan;
                break;
            case evChanType:
                if (curChan >= 0) out.channels[curChan].type = (int) value;
                break;
            case evSamplePath:
                if (curChan >= 0) out.channels[curChan].samplePath = decodeText (data, len, utf16);
                break;
            case evPluginName:
            case evChanName0:
            case evChanName2:
                if (curChan >= 0) { auto t = decodeText (data, len, utf16); if (t.isNotEmpty()) out.channels[curChan].name = t; }
                break;
            case evDefPlugin:
                if (curChan >= 0) out.channels[curChan].plugin = decodeText (data, len, utf16);
                break;

            case evPatNew:
                curPat = (int) value; curChan = -1;
                out.patterns[curPat].id = curPat;
                break;
            case evPatName:
                if (curPat >= 0) out.patterns[curPat].name = decodeText (data, len, utf16);
                break;
            case evPatNotes:
                if (curPat >= 0 && data != nullptr)
                {
                    auto& notes = out.patterns[curPat].notes;
                    for (size_t o = 0; o + 24 <= len; o += 24)
                    {
                        const uint8* n = data + o;
                        FlpNote note;
                        note.position = rd32 (n + 0);
                        note.channel  = rd16 (n + 6);
                        note.length   = rd32 (n + 8);
                        note.key      = (int) rd16 (n + 12);
                        note.velocity = n[21]; // 16 fine pitch, 18 release, 19 midi ch, 20 pan, 21 vel
                        notes.push_back (note);
                    }
                }
                break;

            case evSlotIndex: case evMixerParams: case evInsertFlags: case evInsertRouting:
                curChan = -1; // on est passé dans le mixer : les noms de plugins ne concernent plus les channels
                break;
            default: break;
        }
    }

    if (out.channels.empty() && out.patterns.empty()) { error = String::fromUTF8 ("Aucune donnée lisible dans ce .flp."); return false; }
    return true;
}

//==============================================================================
static Array<File> flStudioAppDirs()
{
    Array<File> res;
   #if JUCE_MAC
    for (auto& e : RangedDirectoryIterator (File ("/Applications"), false, "FL Studio*", File::findDirectories))
        res.add (e.getFile().getChildFile ("Contents/Resources/FL"));
   #elif JUCE_WINDOWS
    for (auto& e : RangedDirectoryIterator (File ("C:/Program Files/Image-Line"), false, "FL Studio*", File::findDirectories))
        res.add (e.getFile());
   #endif
    return res;
}

static void indexFolder (const File& dir, std::map<String, File>& index, int& budget)
{
    if (! dir.isDirectory()) return;
    for (auto& e : RangedDirectoryIterator (dir, true, "*", File::findFiles))
    {
        if (--budget < 0) return;
        const auto key = e.getFile().getFileName().toLowerCase();
        if (index.find (key) == index.end()) index[key] = e.getFile();
    }
}

void resolveSamples (FlpProject& p, const File& extraSearchDir)
{
    const auto appDirs = flStudioAppDirs();
    const File userData = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Image-Line/FL Studio");

    std::map<String, File> index;
    bool indexed = false;

    for (auto& [id, ch] : p.channels)
    {
        if (ch.samplePath.isEmpty()) continue;
        const String raw = ch.samplePath.replaceCharacter ('\\', '/');
        Array<File> cands;

        auto addPath = [&cands] (const String& s)
        {
            if (File::isAbsolutePath (s)) cands.add (File (s));
        };

        if (raw.containsIgnoreCase ("%FLStudioFactoryData%"))
            for (auto& a : appDirs) addPath (raw.replace ("%FLStudioFactoryData%", a.getFullPathName(), true));
        if (raw.containsIgnoreCase ("%FLStudioUserData%"))
            addPath (raw.replace ("%FLStudioUserData%", userData.getFullPathName(), true));
        if (! raw.startsWith ("%")) addPath (raw);

        ch.resolved = File();
        for (auto& c : cands)
            if (c.existsAsFile()) { ch.resolved = c; break; }

        if (! ch.resolved.existsAsFile())
        {
            if (! indexed)
            {
                int budget = 60000;
                indexFolder (extraSearchDir, index, budget);
                indexFolder (p.file.getParentDirectory(), index, budget);
                indexFolder (userData, index, budget);
                indexed = true;
            }
            const auto fname = raw.fromLastOccurrenceOf ("/", false, false).toLowerCase();
            auto it = index.find (fname);
            if (it != index.end()) ch.resolved = it->second;
        }
    }
}

//==============================================================================
static File uniqueChild (const File& dir, const String& base, const String& ext)
{
    auto f = dir.getChildFile (safeFileName (base) + ext);
    int n = 2;
    while (f.exists()) f = dir.getChildFile (safeFileName (base) + " (" + String (n++) + ")" + ext);
    return f;
}

static void addTrack (MidiFile& mf, const std::vector<const FlpNote*>& notes, const String& name, double tempo, bool withTempo, int ppq)
{
    MidiMessageSequence seq;
    auto nameMsg = MidiMessage::textMetaEvent (3, name);
    nameMsg.setTimeStamp (0);
    seq.addEvent (nameMsg);
    if (withTempo)
    {
        auto t = MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / jmax (10.0, tempo)));
        t.setTimeStamp (0);
        seq.addEvent (t);
        auto ts = MidiMessage::timeSignatureMetaEvent (4, 4);
        ts.setTimeStamp (0);
        seq.addEvent (ts);
    }
    for (auto* n : notes)
    {
        const int key = jlimit (0, 127, n->key);
        const auto vel = (uint8) jlimit (1, 127, n->velocity);
        seq.addEvent (MidiMessage::noteOn (1, key, vel), (double) n->position);
        const uint32 len = n->length > 0 ? n->length : (uint32) jmax (1, ppq / 4); // step seq : 1 double-croche
        seq.addEvent (MidiMessage::noteOff (1, key), (double) (n->position + len));
    }
    seq.updateMatchedPairs();
    seq.sort();
    mf.addTrack (seq);
}

static bool writeMidi (const MidiFile& mf, const File& dest)
{
    dest.deleteFile();
    FileOutputStream os (dest);
    return os.openedOk() && mf.writeTo (os, 1);
}

FlpExportResult exportFlp (const FlpProject& p, const File& destRoot, const std::set<int>* patSel, const std::set<int>* chanSel)
{
    FlpExportResult r;
    r.folder = uniqueChild (destRoot, p.title + " - K1T", {});
    const auto midiDir = r.folder.getChildFile ("MIDI");
    const auto smpDir  = r.folder.getChildFile ("Samples");
    midiDir.createDirectory();

    auto chanName = [&p] (int id)
    {
        auto it = p.channels.find (id);
        return it != p.channels.end() ? it->second.displayName() : "Channel " + String (id + 1);
    };

    int idx = 1;
    for (auto& [pid, pat] : p.patterns)
    {
        if (pat.notes.empty()) continue;
        if (patSel != nullptr && patSel->count (pid) == 0) { ++idx; continue; }
        const String patName = String (idx++).paddedLeft ('0', 2) + " - " + (pat.name.isNotEmpty() ? pat.name : "Pattern " + String (pid));

        std::map<int, std::vector<const FlpNote*>> byChan;
        for (auto& n : pat.notes) byChan[n.channel].push_back (&n);

        // 1 fichier multi-pistes par pattern
        MidiFile all;
        all.setTicksPerQuarterNote (p.ppq);
        std::vector<const FlpNote*> none;
        addTrack (all, none, patName, p.tempo, true, p.ppq);
        for (auto& [cid, notes] : byChan) addTrack (all, notes, chanName (cid), p.tempo, false, p.ppq);
        if (writeMidi (all, uniqueChild (midiDir, patName, ".mid"))) ++r.midiFiles;

        // + 1 fichier par instrument
        if (byChan.size() > 1)
        {
            const auto sub = midiDir.getChildFile (safeFileName (patName));
            sub.createDirectory();
            for (auto& [cid, notes] : byChan)
            {
                MidiFile one;
                one.setTicksPerQuarterNote (p.ppq);
                addTrack (one, notes, chanName (cid), p.tempo, true, p.ppq);
                if (writeMidi (one, uniqueChild (sub, chanName (cid), ".mid"))) ++r.midiFiles;
            }
        }
    }

    StringArray missing;
    std::map<String, bool> copied;
    for (auto& [id, ch] : p.channels)
    {
        if (ch.samplePath.isEmpty()) continue;
        if (chanSel != nullptr && chanSel->count (id) == 0) continue;
        if (ch.resolved.existsAsFile())
        {
            const auto key = ch.resolved.getFullPathName();
            if (copied.count (key)) continue;
            copied[key] = true;
            smpDir.createDirectory();
            if (ch.resolved.copyFileTo (uniqueChild (smpDir, ch.resolved.getFileNameWithoutExtension(), ch.resolved.getFileExtension())))
                ++r.samplesCopied;
        }
        else
        {
            missing.add (ch.displayName() + String::fromUTF8 ("  →  ") + ch.samplePath);
            ++r.samplesMissing;
        }
    }
    if (! missing.isEmpty())
        r.folder.getChildFile ("samples introuvables.txt").replaceWithText (missing.joinIntoString ("\n"));
    if (midiDir.getNumberOfChildFiles (File::findFilesAndDirectories) == 0) midiDir.deleteRecursively();

    return r;
}

//==============================================================================
PatternRender renderPattern (const FlpProject& p, const FlpPattern& pat,
                             const std::function<SamplePtr (const File&)>& load, double sr)
{
    PatternRender res;
    if (pat.notes.empty()) return res;

    const double secPerTick = 60.0 / (jmax (10.0, p.tempo) * jmax (1, p.ppq));
    uint32 endTick = 0;
    for (auto& n : pat.notes) endTick = jmax (endTick, n.position + jmax<uint32> (n.length, (uint32) p.ppq / 4));
    const uint32 bar = (uint32) (4 * p.ppq);
    const uint32 loopTicks = jmax (bar, ((endTick + bar - 1) / bar) * bar);
    const int loopLen = (int) std::ceil (loopTicks * secPerTick * sr);
    if (loopLen < 16) return res;

    AudioBuffer<float> out (2, loopLen);
    out.clear();

    std::map<int, SamplePtr> smp;
    std::set<int> played, silent;
    for (auto& n : pat.notes)
    {
        if (smp.count (n.channel) == 0)
        {
            SamplePtr s;
            auto it = p.channels.find (n.channel);
            if (it != p.channels.end() && it->second.resolved.existsAsFile()) s = load (it->second.resolved);
            smp[n.channel] = s;
        }
        auto s = smp[n.channel];
        if (s == nullptr) { silent.insert (n.channel); continue; }
        played.insert (n.channel);

        // C5 (60) = hauteur d'origine, comme le Sampler de FL
        const double step = (s->sr / sr) * std::pow (2.0, jlimit (-48, 48, n.key - 60) / 12.0);
        const int start = (int) std::round (n.position * secPerTick * sr);
        int dur = (int) std::floor ((s->length() - 1) / step);
        dur = jmin (dur, loopLen);
        int fadeLen = 0;
        if (n.length > 0) // note du piano roll : on coupe à la fin de la note (+ petit release)
        {
            const int noteLen = (int) std::round (n.length * secPerTick * sr);
            fadeLen = (int) (0.03 * sr);
            if (noteLen + fadeLen < dur) dur = noteLen + fadeLen; else fadeLen = 0;
        }
        const float g = jlimit (0.0f, 1.3f, (float) n.velocity / 100.0f);
        for (int c = 0; c < 2; ++c)
        {
            const float* x = s->buf.getReadPointer (c);
            for (int i = 0; i < dur; ++i)
            {
                const double pos = i * step;
                const int i0 = (int) pos;
                const float fr = (float) (pos - i0);
                float v = (x[i0] + (x[i0 + 1] - x[i0]) * fr) * g;
                if (fadeLen > 0 && i > dur - fadeLen) v *= (float) (dur - i) / (float) fadeLen;
                out.addSample (c, (start + i) % loopLen, v); // ce qui dépasse revient au début : boucle sans coupure
            }
        }
    }
    for (auto c : played) silent.erase (c);

    float pk = 0;
    for (int c = 0; c < 2; ++c) pk = jmax (pk, out.getMagnitude (c, 0, loopLen));
    if (pk > 0.98f) out.applyGain (0.98f / pk);

    auto sd = std::make_shared<SampleData>();
    sd->sr = sr;
    sd->name = pat.name.isNotEmpty() ? pat.name : "Pattern " + String (pat.id);
    sd->buf = std::move (out);
    res.audio = sd;
    res.channelsPlayed = (int) played.size();
    res.channelsSilent = (int) silent.size();
    res.seconds = loopLen / sr;
    return res;
}
} // namespace k1t
