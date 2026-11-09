#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace k1t;

K1TProcessor::K1TProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true))
{
    startTimerHz (30);
}

K1TProcessor::~K1TProcessor() { stopTimer(); }

bool K1TProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void K1TProcessor::prepareToPlay (double sampleRate, int)
{
    hostSr = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& v : voices)
    {
        if (v.active) v.s->playingVoices.fetch_sub (1);
        v.active = false;
    }
}

File K1TProcessor::appFolder()
{
    auto f = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("K1T");
    f.createDirectory();
    return f;
}

File K1TProcessor::exportForDrag (const SampleData& s, const String& name)
{
    auto f = appFolder().getChildFile ("Exports").getChildFile (safeFileName (name) + ".wav");
    writeWav (s, f);
    return f;
}

File K1TProcessor::kitFolder()
{
    if (kitName.isEmpty())
        kitName = "K1T Kit " + Time::getCurrentTime().formatted ("%d-%m-%Y");
    auto f = appFolder().getChildFile ("Kits").getChildFile (safeFileName (kitName));
    f.createDirectory();
    return f;
}

static String kitCategory (const String& n)
{
    const auto s = n.toLowerCase();
    auto has = [&s] (std::initializer_list<const char*> keys) { for (auto* k : keys) if (s.contains (k)) return true; return false; };
    if (has ({ "open", "oh " , "ohat", "o-hat" }))          return "Open Hats";
    if (has ({ "hat", "hh", "hihat", "hi-hat", "chh" }))    return "Hats";
    if (has ({ "808", "bass", "sub" }))                      return "808s";
    if (has ({ "kick", "kck", "bd " }))                      return "Kicks";
    if (has ({ "snare", "snr", "rim" }))                     return "Snares";
    if (has ({ "clap", "clp", "snap" }))                     return "Claps";
    if (has ({ "crash", "ride", "cymbal", "cym" }))          return "Cymbals";
    if (has ({ "perc", "tom", "shaker", "conga", "bongo", "cowbell", "bell" })) return "Percs";
    return "One Shots";
}

File K1TProcessor::addToKit (const SampleData& s, const String& name, const String& categoryHint)
{
    const auto cat = kitCategory (categoryHint.isNotEmpty() ? categoryHint + " " + name : name);
    const auto dir = kitFolder().getChildFile (cat);
    auto f = dir.getChildFile (safeFileName (name) + ".wav");
    for (int n = 2; f.exists(); ++n)
        f = dir.getChildFile (safeFileName (name) + " " + String (n) + ".wav");
    if (! writeWav (s, f)) return {};
    ++kitCount;
    return f;
}

//==============================================================================
void K1TProcessor::keepAlive (SamplePtr s)
{
    if (s != nullptr) graveyard.push_back ({ std::move (s), Time::getMillisecondCounterHiRes() });
}

void K1TProcessor::timerCallback()
{
    if (renderDirty) renderNow();
    if (mixPending && Time::getMillisecondCounterHiRes() - mixChangedAt > 450.0) commitMix();

    // un sample remplacé n'est libéré que lorsqu'aucune voix ne le lit plus (+ petite marge)
    const double now = Time::getMillisecondCounterHiRes();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const Graveyard& g) { return now - g.time > 300.0 && g.s->playingVoices.load() <= 0; }),
                     graveyard.end());
}

void K1TProcessor::renderNow()
{
    renderDirty = false;
    auto newKit = renderKit (layers, fx, spreadMs, spreadSeed, 44100.0);
    keepAlive (kit);
    kit = newKit;
    kitRaw.store (kit.get());
    chokeRaw.store (choke);
    sendChangeMessage();
}

String K1TProcessor::loadLayer (int index, const File& f)
{
    String err;
    auto s = loadAudioFile (f, err);
    if (s == nullptr) return err;
    setLayer (index, s, f.getFullPathName());
    return {};
}

void K1TProcessor::setLayer (int index, SamplePtr s, const String& path)
{
    if (! isPositiveAndBelow (index, kNumLayers)) return;
    auto& L = layers[(size_t) index];
    keepAlive (L.src);
    L = Layer();
    L.src = std::move (s);
    L.path = path;
    requestRender();
}

void K1TProcessor::clearLayer (int index)
{
    if (! isPositiveAndBelow (index, kNumLayers)) return;
    keepAlive (layers[(size_t) index].src);
    layers[(size_t) index] = Layer();
    requestRender();
}

int K1TProcessor::firstEmptyLayer() const
{
    for (int i = 0; i < kNumLayers; ++i)
        if (layers[(size_t) i].src == nullptr) return i;
    return -1;
}

//==============================================================================
String K1TProcessor::loadReference (const File& f)
{
    String err;
    auto s = loadAudioFile (f, err);
    if (s == nullptr) return err;
    keepAlive (reference);
    reference = s;
    referencePath = f.getFullPathName();
    return {};
}

void K1TProcessor::generate (bool newSeed)
{
    if (reference == nullptr) return;
    if (! variants.empty())
    {
        genHistory.push_back ({ variants, genSeed, genMode, reference, referencePath, genAmount });
        if (genHistory.size() > 30) genHistory.erase (genHistory.begin());
    }
    if (newSeed || genSeed == 0) genSeed = (uint32) Random::getSystemRandom().nextInt (0x7fffffff) + 1;
    variants = genMode == 1 ? generate808Variants (*reference, 8, genAmount, genSeed)
                            : generateVariants (*reference, 8, genAmount, genSeed);
}

bool K1TProcessor::undoGenerate()
{
    if (genHistory.empty()) return false;
    auto b = genHistory.back();
    genHistory.pop_back();
    for (auto& v : variants) keepAlive (v);
    variants = b.variants;
    genSeed = b.seed;
    genMode = b.mode;
    genAmount = b.amount;
    if (b.ref != nullptr && b.ref != reference) { keepAlive (reference); reference = b.ref; referencePath = b.refPath; }
    return true;
}

//==============================================================================
// Retour en arrière (MIX)
K1TProcessor::MixState K1TProcessor::captureMix() const
{
    MixState m;
    m.layers = layers; m.fx = fx; m.spreadMs = spreadMs; m.spreadSeed = spreadSeed; m.choke = choke;
    return m;
}

void K1TProcessor::applyMix (const MixState& m)
{
    for (auto& L : layers) keepAlive (L.src);
    layers = m.layers; fx = m.fx; spreadMs = m.spreadMs; spreadSeed = m.spreadSeed; choke = m.choke;
    restoringMix = true;
    requestRender();
    restoringMix = false;
}

String K1TProcessor::signature (const MixState& m)
{
    String s = m.fx.toTree().toXmlString() + String (m.spreadMs) + String ((int) m.spreadSeed) + (m.choke ? "c" : "-");
    for (auto& L : m.layers)
        s << String::toHexString ((pointer_sized_int) L.src.get()) << L.gainDb << L.pitch << L.nudgeMs << L.pan << L.decay << L.tone << (int) L.mute << (int) L.reverse;
    return s;
}

void K1TProcessor::commitMix()
{
    mixPending = false;
    auto cur = captureMix();
    if (signature (cur) == signature (mixCommitted)) return;
    mixUndo.push_back (mixCommitted);
    if (mixUndo.size() > 60) mixUndo.erase (mixUndo.begin());
    mixCommitted = cur;
}

bool K1TProcessor::undoMix()
{
    if (mixPending) commitMix();
    if (mixUndo.empty()) return false;
    auto prev = mixUndo.back();
    mixUndo.pop_back();
    applyMix (prev);
    mixCommitted = prev;
    mixPending = false;
    renderNow();
    return true;
}

//==============================================================================
void K1TProcessor::preview (SamplePtr s)
{
    if (s == nullptr) return;
    if (previewHeld != s) keepAlive (previewHeld);
    previewHeld = s;
    pendingLoop.store (false);
    pendingPreview.store (s.get());
}

void K1TProcessor::previewLoop (SamplePtr s)
{
    if (s == nullptr) return;
    if (previewHeld != s) keepAlive (previewHeld);
    previewHeld = s;
    pendingLoop.store (true);
    pendingPreview.store (s.get());
}

void K1TProcessor::startVoice (const SampleData* s, int note, float velocity, bool fromMidi, bool loop)
{
    if (s == nullptr || s->length() < 2) return;

    const bool isKit = fromMidi;
    if (! fromMidi || chokeRaw.load())
        for (auto& v : voices)
            if (v.active && (fromMidi ? v.isKit : ! v.isKit))
                v.fadeStep = 1.0f / (float) (0.004 * hostSr); // coupe en douceur

    Voice* target = nullptr;
    for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices) if (v.age < target->age) target = &v;
        target->s->playingVoices.fetch_sub (1);
    }
    s->playingVoices.fetch_add (1);

    target->s = s;
    target->pos = 0;
    target->step = (s->sr / hostSr) * std::pow (2.0, jlimit (-36, 36, note - 60) / 12.0);
    target->gain = velocity;
    target->fade = 1.0f;
    target->fadeStep = 0.0f;
    target->active = true;
    target->isKit = isKit;
    target->loop = loop;
    if (! fromMidi) loopRaw.store (loop ? s : nullptr);
    target->age = ++voiceCounter;
}

void K1TProcessor::renderVoices (AudioBuffer<float>& out, int start, int num)
{
    if (num <= 0) return;
    const int outCh = out.getNumChannels();
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const int len = v.s->length();
        const float* l = v.s->buf.getReadPointer (0);
        const float* r = v.s->buf.getReadPointer (1);
        for (int i = 0; i < num; ++i)
        {
            const int idx = (int) v.pos;
            if (v.loop && idx >= len - 1) { v.pos -= (double) (len - 1); --i; continue; }
            if (idx >= len - 1 || v.fade <= 0.0f)
            {
                if (v.loop) { const SampleData* expected = v.s; loopRaw.compare_exchange_strong (expected, nullptr); }
                v.active = false; v.s->playingVoices.fetch_sub (1); break;
            }
            const float t = (float) (v.pos - idx);
            const float g = v.gain * v.fade;
            const float sl = (l[idx] + (l[idx + 1] - l[idx]) * t) * g;
            const float sr = (r[idx] + (r[idx + 1] - r[idx]) * t) * g;
            if (outCh >= 2) { out.addSample (0, start + i, sl); out.addSample (1, start + i, sr); }
            else              out.addSample (0, start + i, 0.5f * (sl + sr));
            v.pos += v.step;
            v.fade -= v.fadeStep;
        }
    }
}

void K1TProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();

    if (stopRequest.exchange (false))
        for (auto& v : voices) if (v.active) v.fadeStep = 1.0f / (float) (0.004 * hostSr);

    if (auto* p = pendingPreview.exchange (nullptr))
        startVoice (p, 60, 0.9f, false, pendingLoop.load());

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = jlimit (0, n, meta.samplePosition);
        renderVoices (buffer, pos, at - pos);
        pos = at;
        if (m.isNoteOn())
        {
            const float vel = std::pow (m.getFloatVelocity(), 1.2f);
            startVoice (kitRaw.load(), m.getNoteNumber(), vel, true);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices) if (v.active) v.fadeStep = 1.0f / (float) (0.004 * hostSr);
    }
    renderVoices (buffer, pos, n - pos);

    // infos UI
    const Voice* newest = nullptr;
    for (auto& v : voices)
        if (v.active && (newest == nullptr || v.age > newest->age)) newest = &v;
    if (newest != nullptr)
    {
        lastPlayed.store (newest->s);
        lastPos.store ((float) (newest->pos / jmax (1, newest->s->length())));
    }
    else lastPos.store (-1.0f);

    float pk = 0;
    for (int c = 0; c < buffer.getNumChannels(); ++c) pk = jmax (pk, buffer.getMagnitude (c, 0, n));
    outLevel.store (jmax (pk, outLevel.load() * 0.85f));
}

//==============================================================================
void K1TProcessor::getStateInformation (MemoryBlock& dest)
{
    ValueTree st ("K1T");
    st.setProperty ("version", 1, nullptr);
    for (int i = 0; i < kNumLayers; ++i)
    {
        const auto& L = layers[(size_t) i];
        ValueTree t ("LAYER");
        t.setProperty ("path", L.path, nullptr);
        t.setProperty ("gain", L.gainDb, nullptr);  t.setProperty ("pitch", L.pitch, nullptr);
        t.setProperty ("nudge", L.nudgeMs, nullptr); t.setProperty ("pan", L.pan, nullptr);
        t.setProperty ("decay", L.decay, nullptr);  t.setProperty ("tone", L.tone, nullptr);
        t.setProperty ("mute", L.mute, nullptr);    t.setProperty ("rev", L.reverse, nullptr);
        st.appendChild (t, nullptr);
    }
    st.appendChild (fx.toTree(), nullptr);
    st.setProperty ("spread", spreadMs, nullptr);
    st.setProperty ("seed", (int) spreadSeed, nullptr);
    st.setProperty ("choke", choke, nullptr);
    st.setProperty ("ref", referencePath, nullptr);
    st.setProperty ("amount", genAmount, nullptr);
    st.setProperty ("genMode", genMode, nullptr);
    st.setProperty ("genSeed", (int) genSeed, nullptr);
    st.setProperty ("tab", currentTab, nullptr);
    st.setProperty ("kitName", kitName, nullptr);
    st.setProperty ("kitCount", kitCount, nullptr);

    MemoryOutputStream os (dest, false);
    st.writeToStream (os);
}

void K1TProcessor::setStateInformation (const void* data, int size)
{
    auto st = ValueTree::readFromData (data, (size_t) size);
    if (! st.hasType ("K1T")) return;

    int li = 0;
    for (auto t : st)
    {
        if (! t.hasType ("LAYER") || li >= kNumLayers) continue;
        auto& L = layers[(size_t) li];
        L = Layer();
        const String path = t.getProperty ("path");
        if (path.isNotEmpty() && File (path).existsAsFile())
        {
            String err;
            L.src = loadAudioFile (File (path), err);
            if (L.src != nullptr) L.path = path;
        }
        L.gainDb = t.getProperty ("gain", 0.0f);  L.pitch = t.getProperty ("pitch", 0.0f);
        L.nudgeMs = t.getProperty ("nudge", 0.0f); L.pan = t.getProperty ("pan", 0.0f);
        L.decay = t.getProperty ("decay", 1.0f);  L.tone = t.getProperty ("tone", 0.0f);
        L.mute = t.getProperty ("mute", false);   L.reverse = t.getProperty ("rev", false);
        ++li;
    }
    fx.fromTree (st.getChildWithName ("FX"));
    spreadMs = st.getProperty ("spread", 0.0f);
    spreadSeed = (uint32) (int) st.getProperty ("seed", 1);
    choke = st.getProperty ("choke", false);
    genAmount = st.getProperty ("amount", 0.5f);
    genMode = st.getProperty ("genMode", 0);
    currentTab = st.getProperty ("tab", 0);
    kitName = st.getProperty ("kitName", String());
    kitCount = st.getProperty ("kitCount", 0);

    const String ref = st.getProperty ("ref");
    if (ref.isNotEmpty() && File (ref).existsAsFile() && loadReference (File (ref)).isEmpty())
    {
        genSeed = (uint32) (int) st.getProperty ("genSeed", 0);
        if (genSeed != 0) generate (false);
    }
    renderNow();
    mixUndo.clear();
    mixCommitted = captureMix();
    mixPending = false;
}

AudioProcessorEditor* K1TProcessor::createEditor() { return new K1TEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new K1TProcessor(); }
