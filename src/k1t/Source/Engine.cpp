#include "Engine.h"

namespace k1t
{
using namespace juce;

//==============================================================================
float SampleData::peak() const
{
    float p = 0.0f;
    for (int c = 0; c < buf.getNumChannels(); ++c)
        p = jmax (p, buf.getMagnitude (c, 0, buf.getNumSamples()));
    return p;
}


SamplePtr loadAudioFile (const File& f, String& error)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = String::fromUTF8 ("Format non supporté : ") + f.getFileName(); return nullptr; }

    const auto maxLen = (int64) (r->sampleRate * 30.0);
    const auto len = (int) jmin (r->lengthInSamples, maxLen);
    if (len <= 0) { error = "Fichier vide : " + f.getFileName(); return nullptr; }

    auto s = std::make_shared<SampleData>();
    s->sr = r->sampleRate;
    s->name = f.getFileNameWithoutExtension();
    s->buf.setSize (2, len);
    s->buf.clear();

    AudioBuffer<float> tmp ((int) jmax (1u, r->numChannels), len);
    r->read (&tmp, 0, len, 0, true, true);
    if (tmp.getNumChannels() == 1)
    {
        s->buf.copyFrom (0, 0, tmp, 0, 0, len);
        s->buf.copyFrom (1, 0, tmp, 0, 0, len);
    }
    else
    {
        s->buf.copyFrom (0, 0, tmp, 0, 0, len);
        s->buf.copyFrom (1, 0, tmp, 1, 0, len);
    }
    return s;
}

bool writeWav (const SampleData& s, const File& dest)
{
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    std::unique_ptr<FileOutputStream> out (dest.createOutputStream());
    if (out == nullptr) return false;

    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (out.get(), s.sr, 2, 24, {}, 0));
    if (w == nullptr) return false;
    out.release();
    return w->writeFromAudioSampleBuffer (s.buf, 0, s.buf.getNumSamples());
}

String safeFileName (String s)
{
    s = File::createLegalFileName (s.trim());
    return s.isEmpty() ? String ("sans nom") : s;
}

//==============================================================================
AudioBuffer<float> resample (const AudioBuffer<float>& in, double step)
{
    const int inLen = in.getNumSamples();
    const int chans = in.getNumChannels();
    if (inLen < 2 || step <= 0.0) return in;
    if (std::abs (step - 1.0) < 1.0e-6) return in;

    AudioBuffer<float> src (in);
    if (step > 1.02) // anti-repliement quand on monte le pitch
    {
        for (int c = 0; c < chans; ++c)
            for (int pass = 0; pass < 2; ++pass)
            {
                IIRFilter f;
                f.setCoefficients (IIRCoefficients::makeLowPass (1.0, jlimit (0.05, 0.48, 0.46 / step), 0.707));
                f.processSamples (src.getWritePointer (c), inLen);
            }
    }

    const int outLen = jmax (1, (int) std::floor ((inLen - 1) / step) + 1);
    AudioBuffer<float> out (chans, outLen);
    for (int c = 0; c < chans; ++c)
    {
        const float* x = src.getReadPointer (c);
        float* y = out.getWritePointer (c);
        for (int i = 0; i < outLen; ++i)
        {
            const double pos = i * step;
            const int i1 = (int) pos;
            const float t = (float) (pos - i1);
            const float xm1 = x[jmax (0, i1 - 1)];
            const float x0  = x[jmin (inLen - 1, i1)];
            const float x1  = x[jmin (inLen - 1, i1 + 1)];
            const float x2  = x[jmin (inLen - 1, i1 + 2)];
            // Hermite 4 points
            const float c0 = x0;
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            y[i] = ((c3 * t + c2) * t + c1) * t + c0;
        }
    }
    return out;
}

void normalisePeak (AudioBuffer<float>& b, float targetDb)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        p = jmax (p, b.getMagnitude (c, 0, b.getNumSamples()));
    if (p > 1.0e-6f)
        b.applyGain (Decibels::decibelsToGain (targetDb) / p);
}

void trimSilence (AudioBuffer<float>& b, double sr, float thresholdDb)
{
    const float th = Decibels::decibelsToGain (thresholdDb);
    int last = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        for (int i = b.getNumSamples() - 1; i > last; --i)
            if (std::abs (x[i]) > th) { last = i; break; }
    }
    const int newLen = jmin (b.getNumSamples(), last + 1 + (int) (0.002 * sr));
    if (newLen < b.getNumSamples() && newLen > 16)
        b.setSize (b.getNumChannels(), newLen, true, true, true);

    // micro fondu de sortie pour éviter les clics
    const int fade = jmin (b.getNumSamples() / 4, (int) (0.003 * sr));
    if (fade > 1)
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.applyGainRamp (c, b.getNumSamples() - fade, fade, 1.0f, 0.0f);
}

//==============================================================================
const char* fxName (int id)
{
    switch (id)
    {
        case fxTransient: return "TRANSIENT";
        case fxEq:        return "EQ";
        case fxFilter:    return "FILTER";
        case fxDrive:     return "DISTORTION";
        case fxCrush:     return "BITCRUSH";
        case fxComp:      return "COMPRESSOR";
        case fxWidth:     return "STEREO";
        case fxReverb:    return "REVERB";
        default:          return "";
    }
}

static float rr (Random& r, float a, float b) { return a + r.nextFloat() * (b - a); }
static float rlog (Random& r, float a, float b) { return a * std::pow (b / a, r.nextFloat()); }

void FxParams::randomise (Random& r)
{
    const float prob[kNumFx] = { 0.65f, 0.7f, 0.5f, 0.6f, 0.3f, 0.55f, 0.5f, 0.4f };
    int count = 0;
    for (int i = 0; i < kNumFx; ++i) { on[(size_t) i] = r.nextFloat() < prob[i]; count += on[(size_t) i] ? 1 : 0; }
    while (count < 3)
    {
        auto i = (size_t) r.nextInt (kNumFx);
        if (! on[i]) { on[i] = true; ++count; }
    }

    trAttack = rr (r, 0.15f, 0.75f);   trSustain = rr (r, -0.6f, 0.3f);
    eq[0] = { rlog (r, 50.0f, 160.0f),    rr (r, -3.0f, 4.0f), 0.71f };
    eq[1] = { rlog (r, 180.0f, 900.0f),   rr (r, -5.0f, 2.5f), rr (r, 0.7f, 2.5f) };
    eq[2] = { rlog (r, 1200.0f, 6000.0f), rr (r, -3.0f, 4.0f), rr (r, 0.6f, 2.0f) };
    eq[3] = { rlog (r, 6000.0f, 13000.0f), rr (r, -2.0f, 6.0f), 0.71f };
    hpFreq = rlog (r, 20.0f, 450.0f);  lpFreq = rlog (r, 6000.0f, 20000.0f);
    drive = rr (r, 0.12f, 0.6f);       driveMix = rr (r, 0.4f, 1.0f); driveType = r.nextInt (4);
    crushBits = rr (r, 6.0f, 12.0f);   crushRate = rr (r, 1.0f, 6.0f); crushMix = rr (r, 0.2f, 0.6f);
    compThresh = rr (r, -26.0f, -8.0f); compRatio = rr (r, 2.0f, 8.0f);
    width = rr (r, 1.1f, 1.8f);
    revSize = rr (r, 0.15f, 0.7f);     revMix = rr (r, 0.05f, 0.25f);
}

IIRCoefficients FxParams::eqCoefficients (int band, const EqBand& b, double sr)
{
    const double f = jlimit (20.0, sr * 0.45, (double) b.freq);
    const double q = jlimit (0.2, 12.0, (double) b.q);
    const float g = Decibels::decibelsToGain (jlimit (-24.0f, 24.0f, b.gain));
    switch (band)
    {
        case 0:  return IIRCoefficients::makeLowShelf (sr, f, q, g);
        case kEqBands - 1: return IIRCoefficients::makeHighShelf (sr, f, q, g);
        default: return IIRCoefficients::makePeakFilter (sr, f, q, g);
    }
}

const char* FxParams::eqBandName (int band)
{
    static const char* n[] = { "LOW", "BELL 1", "BELL 2", "HIGH" };
    return n[jlimit (0, kEqBands - 1, band)];
}

ValueTree FxParams::toTree() const
{
    ValueTree t ("FX");
    for (int i = 0; i < kNumFx; ++i) t.setProperty ("on" + String (i), on[(size_t) i], nullptr);
    t.setProperty ("trA", trAttack, nullptr);   t.setProperty ("trS", trSustain, nullptr);
    for (int i = 0; i < kEqBands; ++i)
    {
        t.setProperty ("e" + String (i) + "f", eq[(size_t) i].freq, nullptr);
        t.setProperty ("e" + String (i) + "g", eq[(size_t) i].gain, nullptr);
        t.setProperty ("e" + String (i) + "q", eq[(size_t) i].q, nullptr);
    }
    t.setProperty ("hp", hpFreq, nullptr);      t.setProperty ("lp", lpFreq, nullptr);
    t.setProperty ("dr", drive, nullptr);       t.setProperty ("drM", driveMix, nullptr);
    t.setProperty ("drT", driveType, nullptr);
    t.setProperty ("cb", crushBits, nullptr);   t.setProperty ("cr", crushRate, nullptr);
    t.setProperty ("cm", crushMix, nullptr);
    t.setProperty ("ct", compThresh, nullptr);  t.setProperty ("cR", compRatio, nullptr);
    t.setProperty ("w", width, nullptr);
    t.setProperty ("rs", revSize, nullptr);     t.setProperty ("rm", revMix, nullptr);
    t.setProperty ("og", outGainDb, nullptr);   t.setProperty ("nrm", normalize, nullptr);
    return t;
}

void FxParams::fromTree (const ValueTree& t)
{
    if (! t.isValid()) return;
    FxParams d;
    for (int i = 0; i < kNumFx; ++i) on[(size_t) i] = t.getProperty ("on" + String (i), false);
    trAttack = t.getProperty ("trA", d.trAttack);   trSustain = t.getProperty ("trS", d.trSustain);
    eq = d.eq;
    if (t.hasProperty ("eqL")) // ancien EQ 3 bandes (v0.1)
    {
        eq[0].gain = t.getProperty ("eqL", 0.0f);
        eq[1].gain = t.getProperty ("eqM", 0.0f);
        eq[1].freq = t.getProperty ("eqF", 1500.0f);
        eq[3].gain = t.getProperty ("eqH", 0.0f);
    }
    for (int i = 0; i < kEqBands; ++i)
    {
        auto& b = eq[(size_t) i];
        b.freq = t.getProperty ("e" + String (i) + "f", b.freq);
        b.gain = t.getProperty ("e" + String (i) + "g", b.gain);
        b.q    = t.getProperty ("e" + String (i) + "q", b.q);
    }
    hpFreq = t.getProperty ("hp", d.hpFreq);        lpFreq = t.getProperty ("lp", d.lpFreq);
    drive = t.getProperty ("dr", d.drive);          driveMix = t.getProperty ("drM", d.driveMix);
    driveType = t.getProperty ("drT", d.driveType);
    crushBits = t.getProperty ("cb", d.crushBits);  crushRate = t.getProperty ("cr", d.crushRate);
    crushMix = t.getProperty ("cm", d.crushMix);
    compThresh = t.getProperty ("ct", d.compThresh); compRatio = t.getProperty ("cR", d.compRatio);
    width = t.getProperty ("w", d.width);
    revSize = t.getProperty ("rs", d.revSize);      revMix = t.getProperty ("rm", d.revMix);
    outGainDb = t.getProperty ("og", d.outGainDb);  normalize = t.getProperty ("nrm", d.normalize);
}

//==============================================================================
static void filterAll (AudioBuffer<float>& b, const IIRCoefficients& co)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        IIRFilter f;
        f.setCoefficients (co);
        f.processSamples (b.getWritePointer (c), b.getNumSamples());
    }
}

void applyFx (AudioBuffer<float>& b, double sr, const FxParams& fx)
{
    const int n = b.getNumSamples();
    if (n < 4) return;
    float* L = b.getWritePointer (0);
    float* R = b.getWritePointer (1);

    // --- Transient shaper
    if (fx.on[fxTransient])
    {
        auto coef = [sr] (double secs) { return (float) std::exp (-1.0 / (secs * sr)); };
        const float fa = coef (0.0005), fr = coef (0.02), sa = coef (0.015), sr2 = coef (0.12);
        float envF = 0, envS = 0, g = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float x = jmax (std::abs (L[i]), std::abs (R[i]));
            envF = x > envF ? fa * envF + (1 - fa) * x : fr * envF + (1 - fr) * x;
            envS = x > envS ? sa * envS + (1 - sa) * x : sr2 * envS + (1 - sr2) * x;
            const float att = jlimit (0.0f, 1.0f, (envF - envS) / (envF + 1.0e-6f));
            const float sus = jlimit (0.0f, 1.0f, (envS - envF) / (envS + 1.0e-6f));
            const float target = Decibels::decibelsToGain (fx.trAttack * 15.0f * att + fx.trSustain * 15.0f * sus);
            g += (target - g) * 0.2f;
            L[i] *= g; R[i] *= g;
        }
    }

    // --- EQ 3 bandes
    if (fx.on[fxEq])
    {
        for (int i = 0; i < FxParams::kEqBands; ++i)
            if (std::abs (fx.eq[(size_t) i].gain) > 0.05f)
                filterAll (b, FxParams::eqCoefficients (i, fx.eq[(size_t) i], sr));
    }

    // --- Filtres HP / LP
    if (fx.on[fxFilter])
    {
        if (fx.hpFreq > 21.0f)              filterAll (b, IIRCoefficients::makeHighPass (sr, jmin ((double) fx.hpFreq, sr * 0.45), 0.707));
        if (fx.lpFreq < 19900.0f && fx.lpFreq < sr * 0.45) filterAll (b, IIRCoefficients::makeLowPass (sr, fx.lpFreq, 0.707));
    }

    // --- Distorsion
    if (fx.on[fxDrive])
    {
        const float g = Decibels::decibelsToGain (fx.drive * 30.0f);
        const float mix = fx.driveMix;
        auto shape = [&] (float x)
        {
            const float v = x * g;
            switch (fx.driveType)
            {
                case 1:  return jlimit (-1.0f, 1.0f, v);                              // hard clip
                case 2:  return std::sin (v * 1.3f);                                  // wavefold
                case 3:  return std::tanh (v + 0.25f) - std::tanh (0.25f);            // tube asymétrique
                default: return std::tanh (v);                                        // soft / tape
            }
        };
        for (int i = 0; i < n; ++i)
        {
            L[i] = L[i] + (shape (L[i]) - L[i]) * mix;
            R[i] = R[i] + (shape (R[i]) - R[i]) * mix;
        }
    }

    // --- Bitcrush
    if (fx.on[fxCrush])
    {
        const float q = std::pow (2.0f, jlimit (2.0f, 16.0f, fx.crushBits) - 1.0f);
        const int hold = jmax (1, roundToInt (fx.crushRate));
        float hl = 0, hr = 0;
        for (int i = 0; i < n; ++i)
        {
            if (i % hold == 0) { hl = std::round (L[i] * q) / q; hr = std::round (R[i] * q) / q; }
            L[i] += (hl - L[i]) * fx.crushMix;
            R[i] += (hr - R[i]) * fx.crushMix;
        }
    }

    // --- Compresseur
    if (fx.on[fxComp])
    {
        const float at = (float) std::exp (-1.0 / (0.003 * sr)), rl = (float) std::exp (-1.0 / (0.08 * sr));
        float env = 0;
        const float ratio = jmax (1.0f, fx.compRatio);
        const float makeup = Decibels::decibelsToGain (-fx.compThresh * (1.0f - 1.0f / ratio) * 0.5f);
        for (int i = 0; i < n; ++i)
        {
            const float x = jmax (std::abs (L[i]), std::abs (R[i]));
            env = x > env ? at * env + (1 - at) * x : rl * env + (1 - rl) * x;
            const float lvl = Decibels::gainToDecibels (env, -120.0f);
            const float over = lvl - fx.compThresh;
            const float gr = over > 0 ? -over * (1.0f - 1.0f / ratio) : 0.0f;
            const float g = Decibels::decibelsToGain (gr) * makeup;
            L[i] *= g; R[i] *= g;
        }
    }

    // --- Stéréo (M/S + élargisseur pour les sources mono)
    if (fx.on[fxWidth])
    {
        const int d = jmax (1, (int) (0.0006 * sr));
        std::vector<float> mid ((size_t) n);
        for (int i = 0; i < n; ++i) mid[(size_t) i] = 0.5f * (L[i] + R[i]);
        const float extra = jmax (0.0f, fx.width - 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float m = mid[(size_t) i];
            float s = 0.5f * (L[i] - R[i]) * fx.width;
            if (extra > 0.0f)
                s += extra * 0.45f * ((i >= d ? mid[(size_t) (i - d)] : 0.0f) - m);
            L[i] = m + s; R[i] = m - s;
        }
    }

    // --- Reverb
    if (fx.on[fxReverb] && fx.revMix > 0.001f)
    {
        const int tail = (int) ((0.25 + fx.revSize * 1.6) * sr);
        b.setSize (2, n + tail, true, true, true);
        Reverb rev;
        rev.setSampleRate (sr);
        Reverb::Parameters p;
        p.roomSize = jlimit (0.0f, 1.0f, fx.revSize);
        p.damping = 0.45f;
        p.wetLevel = fx.revMix;
        p.dryLevel = 1.0f - fx.revMix * 0.4f;
        p.width = 1.0f;
        rev.setParameters (p);
        rev.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
    }
}

//==============================================================================
float spreadFactor (uint32 seed, int layer)
{
    if (layer == 0) return 0.0f; // la couche 1 reste la référence temporelle
    Random r ((int64) seed * 7919 + layer * 104729);
    r.nextFloat();
    return r.nextFloat() * 2.0f - 1.0f;
}

SamplePtr renderKit (const std::array<Layer, kNumLayers>& layers, const FxParams& fx,
                     float spreadMs, uint32 spreadSeed, double outSr)
{
    struct Part { AudioBuffer<float> b; double offset; };
    std::vector<Part> parts;

    for (int li = 0; li < kNumLayers; ++li)
    {
        const auto& L = layers[(size_t) li];
        if (! L.active()) continue;
        const float spreadR = spreadFactor (spreadSeed, li);

        const auto& s = *L.src;
        AudioBuffer<float> in (s.buf);
        if (L.reverse) in.reverse (0, in.getNumSamples());

        const double step = (s.sr / outSr) * std::pow (2.0, L.pitch / 12.0);
        auto b = resample (in, step);
        const int len = b.getNumSamples();

        // couleur (tilt)
        if (L.tone > 0.02f)
            filterAll (b, IIRCoefficients::makeHighPass (outSr, 20.0 * std::pow (350.0, (double) L.tone), 0.6));
        else if (L.tone < -0.02f)
            filterAll (b, IIRCoefficients::makeLowPass (outSr, 20000.0 * std::pow (0.03, (double) -L.tone), 0.6));

        // decay
        if (L.decay < 0.999f)
        {
            const int keep = jmax (32, (int) (len * (0.04f + 0.96f * L.decay * L.decay)));
            const int hold = keep / 8;
            for (int c = 0; c < 2; ++c)
            {
                float* x = b.getWritePointer (c);
                for (int i = hold; i < len; ++i)
                {
                    const float t = jlimit (0.0f, 1.0f, (float) (i - hold) / (float) jmax (1, keep - hold));
                    const float e = 0.5f * (1.0f + std::cos (t * MathConstants<float>::pi));
                    x[i] *= e * e;
                }
            }
            b.setSize (2, jmin (len, keep + 8), true, true, true);
        }

        // gain + pan (équi-puissance)
        const float g = Decibels::decibelsToGain (L.gainDb);
        const float ang = (L.pan + 1.0f) * 0.25f * MathConstants<float>::pi;
        b.applyGain (0, 0, b.getNumSamples(), g * std::cos (ang) * MathConstants<float>::sqrt2);
        b.applyGain (1, 0, b.getNumSamples(), g * std::sin (ang) * MathConstants<float>::sqrt2);

        const double offMs = L.nudgeMs + spreadMs * spreadR;
        parts.push_back ({ std::move (b), offMs * 0.001 * outSr });
    }

    if (parts.empty()) return nullptr;

    double minOff = 1.0e9;
    for (auto& p : parts) minOff = jmin (minOff, p.offset);
    int total = 0;
    for (auto& p : parts) total = jmax (total, (int) std::round (p.offset - minOff) + p.b.getNumSamples());

    auto out = std::make_shared<SampleData>();
    out->sr = outSr;
    out->name = "K1T kit";
    out->buf.setSize (2, total);
    out->buf.clear();
    for (auto& p : parts)
    {
        const int start = (int) std::round (p.offset - minOff);
        for (int c = 0; c < 2; ++c)
            out->buf.addFrom (c, start, p.b, c, 0, p.b.getNumSamples());
    }

    normalisePeak (out->buf, -1.0f);
    applyFx (out->buf, outSr, fx);

    if (fx.normalize) normalisePeak (out->buf, jmin (-0.3f, -0.3f + fx.outGainDb));
    else              out->buf.applyGain (Decibels::decibelsToGain (fx.outGainDb));

    trimSilence (out->buf, outSr);
    return out;
}

//==============================================================================
// Générateur : resynthèse spectrale à phases dérivées (même timbre, nouvelle "prise"),
// + étirement temporel, décalage spectral, inclinaison, pitch, et transitoire d'origine.
static float maxShortRms (const std::vector<float>& x, int win)
{
    float best = 0;
    double acc = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        acc += (double) x[i] * x[i];
        if (i >= (size_t) win) acc -= (double) x[i - (size_t) win] * x[i - (size_t) win];
        best = jmax (best, (float) std::sqrt (jmax (0.0, acc) / win));
    }
    return best;
}

std::vector<SamplePtr> generateVariants (const SampleData& ref, int count, float amount, uint32 seed)
{
    std::vector<SamplePtr> result;
    if (ref.length() < 64) return result;

    const double sr = ref.sr;
    constexpr int order = 9, N = 1 << order, hop = N / 4, bins = N / 2 + 1;
    dsp::FFT fft (order);
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * (float) i / (float) N);

    // mono de référence
    AudioBuffer<float> mono (1, ref.length());
    mono.clear();
    mono.addFrom (0, 0, ref.buf, 0, 0, ref.length(), 0.5f);
    mono.addFrom (0, 0, ref.buf, 1, 0, ref.length(), 0.5f);

    Random rnd ((int64) seed);
    amount = jlimit (0.0f, 1.0f, amount);

    for (int v = 0; v < count; ++v)
    {
        const float pitch   = (rnd.nextFloat() * 2 - 1) * (0.4f + 3.6f * amount);
        const float stretch = std::pow (2.0f, (rnd.nextFloat() * 2 - 1) * (0.15f + 0.75f * amount));
        const float shift   = std::pow (2.0f, (rnd.nextFloat() * 2 - 1) * (0.05f + 0.3f * amount));
        const float tilt    = (rnd.nextFloat() * 2 - 1) * (1.0f + 5.0f * amount);  // dB / octave
        const float jitter  = 0.35f + 0.65f * rnd.nextFloat();
        const float bodyDry = (1.0f - amount) * 0.55f * rnd.nextFloat();

        // le pitch est appliqué dans le domaine spectral (pas de ré-échantillonnage → aigus intacts)
        const float shiftTotal = shift * std::pow (2.0f, pitch / 12.0f);
        const int dryLen = mono.getNumSamples();
        const float* dry = mono.getReadPointer (0);

        // --- analyse
        const int frames = jmax (1, (dryLen + N) / hop);
        std::vector<std::vector<float>> mags ((size_t) frames, std::vector<float> ((size_t) bins, 0.0f));
        std::vector<float> fbuf ((size_t) N * 2);
        for (int f = 0; f < frames; ++f)
        {
            const int start = f * hop - N / 2;
            for (int i = 0; i < N; ++i)
            {
                const int k = start + i;
                fbuf[(size_t) i] = (k >= 0 && k < dryLen) ? dry[k] * win[(size_t) i] : 0.0f;
            }
            std::fill (fbuf.begin() + N, fbuf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (fbuf.data(), true);
            for (int k = 0; k < bins; ++k)
                mags[(size_t) f][(size_t) k] = std::hypot (fbuf[(size_t) (2 * k)], fbuf[(size_t) (2 * k + 1)]);
        }

        // --- synthèse
        const int outFrames = jmax (2, (int) std::round (frames * stretch));
        const int outLen = outFrames * hop + N;
        std::vector<float> wet ((size_t) outLen, 0.0f), norm ((size_t) outLen, 0.0f);
        std::vector<float> phase ((size_t) bins);
        for (auto& p : phase) p = rnd.nextFloat() * MathConstants<float>::twoPi;

        std::vector<float> gainTab ((size_t) bins);
        for (int k = 0; k < bins; ++k)
        {
            const float hz = jmax (20.0f, (float) k * (float) sr / N);
            gainTab[(size_t) k] = std::pow (10.0f, tilt * std::log2 (hz / 4000.0f) / 20.0f);
        }

        for (int j = 0; j < outFrames; ++j)
        {
            const float fpos = jmin ((float) frames - 1.001f, (float) j / stretch);
            const int f0 = (int) fpos;
            const float ft = fpos - (float) f0;
            const int f1 = jmin (frames - 1, f0 + 1);

            std::fill (fbuf.begin(), fbuf.end(), 0.0f);
            for (int k = 1; k < bins - 1; ++k)
            {
                const float srcBin = (float) k / shiftTotal;
                const int b0 = (int) srcBin;
                if (b0 >= bins - 1) break;
                const float bt = srcBin - (float) b0;
                auto magAt = [&] (int f) { return mags[(size_t) f][(size_t) b0] * (1 - bt) + mags[(size_t) f][(size_t) (b0 + 1)] * bt; };
                const float m = (magAt (f0) * (1 - ft) + magAt (f1) * ft) * gainTab[(size_t) k];

                phase[(size_t) k] += MathConstants<float>::twoPi * (float) k * hop / N
                                   + (rnd.nextFloat() - 0.5f) * MathConstants<float>::twoPi * jitter;
                fbuf[(size_t) (2 * k)]     = m * std::cos (phase[(size_t) k]);
                fbuf[(size_t) (2 * k + 1)] = m * std::sin (phase[(size_t) k]);
            }
            fft.performRealOnlyInverseTransform (fbuf.data());
            const int start = j * hop;
            for (int i = 0; i < N; ++i)
            {
                wet[(size_t) (start + i)]  += fbuf[(size_t) i] * win[(size_t) i];
                norm[(size_t) (start + i)] += win[(size_t) i] * win[(size_t) i];
            }
        }
        for (size_t i = 0; i < wet.size(); ++i)
            wet[i] = norm[i] > 1.0e-3f ? wet[i] / norm[i] : 0.0f;

        // recentrer (l'analyse commence à -N/2)
        std::vector<float> wetAligned ((size_t) outLen, 0.0f);
        for (int i = 0; i < outLen; ++i)
        {
            const int src = i + N / 2;
            wetAligned[(size_t) i] = src < outLen ? wet[(size_t) src] : 0.0f;
        }

        // même énergie que la source
        std::vector<float> dryVec (dry, dry + dryLen);
        const int rmsWin = jmax (8, (int) (0.01 * sr));
        const float rd = maxShortRms (dryVec, rmsWin), rw = maxShortRms (wetAligned, rmsWin);
        const float wetScale = rw > 1.0e-7f ? rd / rw : 0.0f;

        // --- mixage transitoire d'origine + corps régénéré
        const int attackN = (int) (0.004 * sr), xfadeN = (int) (0.022 * sr);
        auto s = std::make_shared<SampleData>();
        s->sr = sr;
        s->name = ref.name + " v" + String (v + 1).paddedLeft ('0', 2);
        s->buf.setSize (2, outLen);
        float* o = s->buf.getWritePointer (0);
        for (int i = 0; i < outLen; ++i)
        {
            float dW;
            if (i < attackN) dW = 1.0f;
            else if (i < attackN + xfadeN) dW = jmap ((float) (i - attackN) / (float) xfadeN, 1.0f, bodyDry);
            else dW = bodyDry;
            const float d = i < dryLen ? dry[i] : 0.0f;
            o[i] = d * dW + wetAligned[(size_t) i] * wetScale * (1.0f - dW * 0.85f);
        }

        // decay aléatoire supplémentaire
        if (rnd.nextFloat() < 0.5f * amount + 0.15f)
        {
            const int keep = (int) (outLen * (0.45f + 0.55f * rnd.nextFloat()));
            for (int i = keep / 6; i < outLen; ++i)
            {
                const float t = jlimit (0.0f, 1.0f, (float) (i - keep / 6) / (float) jmax (1, keep - keep / 6));
                o[i] *= std::pow (1.0f - t, 2.0f);
            }
        }

        // fondu d'entrée minuscule
        for (int i = 0; i < jmin (outLen, 8); ++i) o[i] *= (float) i / 8.0f;

        s->buf.copyFrom (1, 0, s->buf, 0, 0, outLen);
        normalisePeak (s->buf, -0.5f);
        trimSilence (s->buf, sr, -66.0f);
        result.push_back (s);
    }
    return result;
}
//==============================================================================
// 808 : variantes tonales
float detect808Pitch (const SampleData& ref)
{
    const double sr = ref.sr;
    const int len = ref.length();
    // on analyse le "corps" (après l'attaque et le glide naturel de la 808)
    // fenêtre après la chute de pitch naturelle ; on recule si le son est court
    int a = (int) (0.15 * sr);
    if (len - a < (int) (0.12 * sr)) a = jmax (0, len - (int) (0.12 * sr));
    const int n = jmin (len - a, (int) (0.4 * sr));
    if (n < (int) (0.05 * sr)) return 0.0f;
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = 0.5f * (ref.buf.getSample (0, a + i) + ref.buf.getSample (1, a + i));

    const int minLag = (int) (sr / 250.0), maxLag = jmin (n / 2, (int) (sr / 25.0));
    // fonction de différence (YIN simplifié)
    std::vector<double> d ((size_t) maxLag + 1, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double acc = 0;
        for (int i = 0; i + lag < n; ++i) { const double e = x[(size_t) i] - x[(size_t) (i + lag)]; acc += e * e; }
        d[(size_t) lag] = acc;
    }
    double run = 0;
    std::vector<double> cmnd ((size_t) maxLag + 1, 1.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        run += d[(size_t) lag];
        cmnd[(size_t) lag] = run > 0 ? d[(size_t) lag] * lag / run : 1.0;
    }
    int best = -1;
    for (int lag = minLag; lag < maxLag; ++lag)
        if (cmnd[(size_t) lag] < 0.15 && cmnd[(size_t) lag] <= cmnd[(size_t) lag + 1]) { best = lag; break; }
    if (best < 0)
    {
        double mn = 1e9;
        for (int lag = minLag; lag <= maxLag; ++lag) if (cmnd[(size_t) lag] < mn) { mn = cmnd[(size_t) lag]; best = lag; }
        if (mn > 0.5) return 0.0f;
    }
    // interpolation parabolique
    double lagF = best;
    if (best > 1 && best < maxLag)
    {
        const double y0 = cmnd[(size_t) best - 1], y1 = cmnd[(size_t) best], y2 = cmnd[(size_t) best + 1];
        const double den = y0 - 2 * y1 + y2;
        if (std::abs (den) > 1e-12) lagF += 0.5 * (y0 - y2) / den;
    }
    return (float) (sr / lagF);
}

juce::String noteName (float hz)
{
    if (hz <= 0) return "?";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::round (69.0 + 12.0 * std::log2 (hz / 440.0));
    return String (names[((midi % 12) + 12) % 12]);
}

std::vector<SamplePtr> generate808Variants (const SampleData& ref, int count, float amount, uint32 seed)
{
    std::vector<SamplePtr> result;
    const int len = ref.length();
    if (len < 64) return result;

    const double sr = ref.sr;
    amount = jlimit (0.0f, 1.0f, amount);

    // mono de référence (une 808 doit rester mono pour garder son sub)
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (ref.buf.getSample (0, i) + ref.buf.getSample (1, i));

    Random rnd ((int64) seed * 31 + 808);

    // fondamentale de la référence → C le plus proche (C5 dans FL = la note jouée sans transposer)
    const float f0 = detect808Pitch (ref);
    double tuneRatio = 1.0;
    if (f0 > 0.0f)
    {
        const double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
        const double target = std::round (midi / 12.0) * 12.0; // C le plus proche
        tuneRatio = std::pow (2.0, (target - midi) / 12.0);
    }

    for (int v = 0; v < count; ++v)
    {
        // --- paramètres de la prise (le pitch, lui, ne bouge jamais : toujours en C)
        const float decayPow = std::pow (2.0f, (rnd.nextFloat() * 2.0f - 1.0f) * (0.15f + 0.55f * amount)); // <1 = plus long
        const int   driveType = rnd.nextInt (3);
        const float drive = rnd.nextFloat() < 0.25f ? 0.0f : (0.1f + 0.9f * amount) * rnd.nextFloat();
        const float punchDb = (rnd.nextFloat() * 2.0f - 0.5f) * (2.0f + 4.0f * amount);
        const float subDb = 3.5f * rnd.nextFloat() * (0.4f + 0.6f * amount);
        const float lpHz = 3500.0f + 9000.0f * rnd.nextFloat();

        // --- accordage en C : même rapport de lecture pour toutes les variantes
        std::vector<float> y;
        y.reserve ((size_t) (len / tuneRatio) + 16);
        for (double pos = 0.0; pos < len - 2; pos += tuneRatio)
        {
            const int i0 = (int) pos;
            const float fr = (float) (pos - i0);
            y.push_back (mono[(size_t) i0] + (mono[(size_t) i0 + 1] - mono[(size_t) i0]) * fr);
        }
        const int n = (int) y.size();
        if (n < 32) continue;

        // --- longueur : remodelage de l'enveloppe (env^p)
        {
            const int win = jmax (16, (int) (0.012 * sr));
            std::vector<float> env ((size_t) n);
            double acc = 0;
            for (int i = 0; i < n; ++i)
            {
                acc += (double) y[(size_t) i] * y[(size_t) i];
                if (i >= win) acc -= (double) y[(size_t) (i - win)] * y[(size_t) (i - win)];
                env[(size_t) i] = (float) std::sqrt (jmax (0.0, acc) / win);
            }
            float pk = 1.0e-9f;
            for (auto e : env) pk = jmax (pk, e);
            const int attackHold = (int) (0.03 * sr);
            float g = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                const float e = jmax (1.0e-5f, env[(size_t) i] / pk);
                float target = i < attackHold ? 1.0f : std::pow (e, decayPow - 1.0f);
                target = jlimit (0.0f, 4.0f, target);
                g += (target - g) * 0.002f;
                y[(size_t) i] *= g;
            }
        }

        AudioBuffer<float> b (2, n);
        b.copyFrom (0, 0, y.data(), n);

        // --- sub
        if (subDb > 0.2f)
        {
            IIRFilter f;
            f.setCoefficients (IIRCoefficients::makeLowShelf (sr, 90.0, 0.7, Decibels::decibelsToGain (subDb)));
            f.processSamples (b.getWritePointer (0), n);
        }

        // --- saturation (harmoniques audibles sur petits haut-parleurs)
        normalisePeak (b, -1.0f);
        if (drive > 0.01f)
        {
            float* x = b.getWritePointer (0);
            const float g = 1.0f + drive * 7.0f;
            const float norm = 1.0f / std::tanh (g);
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i] * g;
                float o;
                switch (driveType)
                {
                    case 1:  o = (std::tanh (in + 0.3f) - std::tanh (0.3f)) * norm; break;  // asymétrique
                    case 2:  o = in / (1.0f + std::abs (in)) * (1.0f + 1.0f / g); break;    // doux
                    default: o = std::tanh (in) * norm; break;                              // tape
                }
                x[i] = o;
            }
            IIRFilter lp;
            lp.setCoefficients (IIRCoefficients::makeLowPass (sr, jmin ((double) lpHz, sr * 0.45), 0.7));
            lp.processSamples (x, n);
            // supprime le décalage DC éventuel
            IIRFilter dc;
            dc.setCoefficients (IIRCoefficients::makeHighPass (sr, 18.0, 0.7));
            dc.processSamples (x, n);
        }

        // --- punch (attaque)
        if (std::abs (punchDb) > 0.3f)
        {
            float* x = b.getWritePointer (0);
            const int a = (int) (0.012 * sr);
            for (int i = 0; i < jmin (n, a); ++i)
                x[i] *= Decibels::decibelsToGain (punchDb * (1.0f - (float) i / (float) a));
        }

        // fondus anti-clic
        {
            float* x = b.getWritePointer (0);
            for (int i = 0; i < jmin (n, 24); ++i) x[i] *= (float) i / 24.0f;
            const int fo = jmin (n / 5, (int) (0.04 * sr));
            for (int i = 0; i < fo; ++i) x[n - 1 - i] *= (float) i / (float) fo;
        }

        b.copyFrom (1, 0, b, 0, 0, n);
        auto s = std::make_shared<SampleData>();
        s->sr = sr;
        s->name = ref.name + " C v" + String (v + 1).paddedLeft ('0', 2);
        s->buf = std::move (b);
        normalisePeak (s->buf, -0.5f);
        trimSilence (s->buf, sr, -70.0f);
        result.push_back (s);
    }
    return result;
}
} // namespace k1t
