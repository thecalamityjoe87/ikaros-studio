#include "engine/PitchAnalysis.h"
#include <pitch_detection.h>   // external/pitch-detection
#include <algorithm>
#include <cmath>

namespace ikaros::pitchanalysis
{

namespace
{
    // Voicing: a frame counts as pitched when probabilistic YIN is at least
    // this sure, and its best period is at least this periodic.
    constexpr float minVoicing = 0.5f;
    constexpr float maxAperiodicity = 0.35f;

    // Quieter than this (dB below the loudest frame, or below full scale) is silence.
    constexpr float silenceBelowPeakDb = 50.0f, silenceDb = -60.0f;

    // Notes: a jump of more than this (semitones) lasting longer than
    // splitSeconds starts a new note; shorter notes are dropped (or, joined
    // on to a neighbour, kept as its glide).
    constexpr float splitSemitones = 0.7f;
    constexpr double splitSeconds = 0.04, minNoteSeconds = 0.06;

    // A piece shorter than this whose pitch slides more than glideSemitones
    // on the way into the next note is a glide, not a note.
    constexpr double maxGlideSeconds = 0.15;
    constexpr float glideSemitones = 1.0f;

    // Gaps this short inside a phrase (a frame or two the detector missed) are filled.
    constexpr double maxFilledGapSeconds = 0.012;

    // Not a single voice if fewer of the sounding frames than this are pitched,
    // or more of the pitched ones than this sound like chords.
    constexpr float minVoicedShare = 0.3f, maxChordShare = 0.3f;

    float hzToNote (float hz)   { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

    float median (std::vector<float> v)
    {
        if (v.empty())
            return 0.0f;

        const auto mid = v.begin() + (std::ptrdiff_t) (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return *mid;
    }

    float percentile (std::vector<float> v, float p)
    {
        if (v.empty())
            return 0.0f;

        const auto at = v.begin() + (std::ptrdiff_t) juce::jlimit (0, (int) v.size() - 1, (int) (p * (float) (v.size() - 1)));
        std::nth_element (v.begin(), at, v.end());
        return *at;
    }

    //==============================================================================
    // Is a pitched frame a chord rather than one voice? Two signs, from its
    // spectrum (Hann window): a chord's notes are often heard as the
    // harmonics of a low "fundamental" that isn't there (C, E and G are the
    // 4th, 5th and 6th harmonics of the C two octaves down), so the first
    // two harmonics hold almost nothing; or the pitch found is one of its
    // notes, and much of the sound is between that note's harmonics.
    class ChordCheck
    {
    public:
        ChordCheck (int frameSize, double sampleRate) : rate (sampleRate)
        {
            int order = 1;

            while ((1 << order) < frameSize)
                ++order;

            fft = std::make_unique<juce::dsp::FFT> (order);
            window.resize ((size_t) frameSize);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), window.size(),
                                                                      juce::dsp::WindowingFunction<float>::hann, false);
            data.assign ((size_t) (2 << order), 0.0f);
            size = 1 << order;
        }

        bool isChordLike (const float* frame, int frameSize, float hz)
        {
            std::fill (data.begin(), data.end(), 0.0f);

            for (int i = 0; i < frameSize; ++i)
                data[(size_t) i] = frame[i] * window[(size_t) i];

            fft->performFrequencyOnlyForwardTransform (data.data(), true);   // magnitudes of bins 0..size/2

            const double binHz = rate / size;
            auto power = [&] (double fromHz, double toHz)
            {
                double sum = 0.0;
                const int a = juce::jmax (1, (int) std::ceil (fromHz / binHz));
                const int b = juce::jmin (size / 2, (int) std::floor (toHz / binHz));

                for (int k = a; k <= b; ++k)
                    sum += (double) data[(size_t) k] * data[(size_t) k];

                return sum;
            };

            // Bands round each harmonic: 3% either side, but at least the
            // window's main lobe (two bins).
            const double width = juce::jmax (2.0 * binHz, 0.03 * hz);
            double harmonics[12] {}, comb = 0.0;

            for (int h = 1; h <= 12; ++h)
            {
                harmonics[h - 1] = power (h * hz - width, h * hz + width);
                comb += harmonics[h - 1];
            }

            const double total = power (40.0, 12.5 * hz);
            double firstEight = 0.0;

            for (int h = 0; h < 8; ++h)
                firstEight += harmonics[h];

            if (total <= 0.0 || firstEight <= 0.0)
                return false;

            const bool missingFundamental = (harmonics[0] + harmonics[1]) / firstEight < 0.08;
            const bool betweenHarmonics = comb / total < 0.5;
            return missingFundamental || betweenHarmonics;
        }

    private:
        double rate;
        int size = 0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, data;
    };
}

//==============================================================================
int frameSizeFor (double sampleRate)
{
    // 2048 at 44.1 and 48 kHz, so the lowest notes (60 Hz) fit a few
    // periods in a frame; doubled at 88.2/96 kHz, and again above.
    return sampleRate <= 50000.0 ? 2048 : sampleRate <= 100000.0 ? 4096 : 8192;
}

std::optional<PitchAnalysis> analyse (juce::AudioFormatReader& reader, std::atomic<float>* progress,
                                      const std::atomic<bool>* cancel)
{
    // Read the whole recording, mixed to one channel. (A 3-minute take at
    // 48 kHz is about 35 MB as floats: fine for a background job.)
    const auto length = (int) std::min (reader.lengthInSamples, (juce::int64) std::numeric_limits<int>::max() - 1);
    const int channels = (int) juce::jmax (1u, reader.numChannels);

    if (length <= 0 || reader.sampleRate <= 0.0)
        return std::nullopt;

    std::vector<float> mono ((size_t) length, 0.0f);
    juce::AudioBuffer<float> block (channels, 65536);

    for (int pos = 0; pos < length; pos += block.getNumSamples())
    {
        if (cancel != nullptr && cancel->load())
            return std::nullopt;

        const int num = juce::jmin (block.getNumSamples(), length - pos);

        if (! reader.read (&block, 0, num, pos, true, true))
            return std::nullopt;

        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::addWithMultiply (mono.data() + pos, block.getReadPointer (ch), 1.0f / (float) channels, num);
    }

    return analyse (mono.data(), length, reader.sampleRate, progress, cancel);
}

std::optional<PitchAnalysis> analyse (const float* samples, int numSamples, double sampleRate,
                                      std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    PitchAnalysis a;
    a.sampleRate = sampleRate;
    a.frameSize = frameSizeFor (sampleRate);
    a.hop = a.frameSize / 8;

    const int numFrames = numSamples >= a.frameSize ? 1 + (numSamples - a.frameSize) / a.hop : 0;
    a.pitch.assign ((size_t) numFrames, 0.0f);
    a.voicing.assign ((size_t) numFrames, 0.0f);

    // Loudness first, to tell silence from sound.
    std::vector<float> levelDb ((size_t) numFrames, -200.0f);
    float loudest = -200.0f;

    for (int i = 0; i < numFrames; ++i)
    {
        const float* f = samples + (size_t) i * (size_t) a.hop;
        double sum = 0.0;

        for (int s = 0; s < a.frameSize; ++s)
            sum += (double) f[s] * f[s];

        levelDb[(size_t) i] = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / a.frameSize), -200.0f);
        loudest = juce::jmax (loudest, levelDb[(size_t) i]);
    }

    const float silence = juce::jmax (silenceDb, loudest - silenceBelowPeakDb);

    pitch_alloc::Yin yin (a.frameSize, (int) std::lround (sampleRate), minHz, maxHz);
    ChordCheck chords (a.frameSize, sampleRate);
    int sounding = 0, voiced = 0, chordLike = 0;

    for (int i = 0; i < numFrames; ++i)
    {
        if ((i & 255) == 0)
        {
            if (cancel != nullptr && cancel->load())
                return std::nullopt;

            if (progress != nullptr)
                *progress = (float) i / (float) numFrames;
        }

        if (levelDb[(size_t) i] < silence)
            continue;

        ++sounding;
        const float* frame = samples + (size_t) i * (size_t) a.hop;
        const auto found = yin.probabilistic_pitch (frame);
        a.voicing[(size_t) i] = found.probability;

        if (found.pitch < minHz || found.pitch > maxHz || found.probability < minVoicing || found.aperiodicity > maxAperiodicity)
            continue;

        if (chords.isChordLike (frame, a.frameSize, found.pitch))
        {
            ++chordLike;
            a.voicing[(size_t) i] = 0.0f;   // a chord's notes aren't one pitch
            continue;
        }

        ++voiced;
        a.pitch[(size_t) i] = hzToNote (found.pitch);
    }

    a.singleVoice = sounding > 0
                     && (float) voiced >= minVoicedShare * (float) sounding
                     && (float) chordLike <= maxChordShare * (float) (voiced + chordLike);

    findNotes (a);

    if (progress != nullptr)
        *progress = 1.0f;

    return a;
}

//==============================================================================
void findNotes (PitchAnalysis& a)
{
    a.notes.clear();

    if (! a.singleVoice)
        return;

    const int numFrames = (int) a.pitch.size();
    const double frameSeconds = a.frameSeconds();
    auto& p = a.pitch;

    // 1. Fill holes of a frame or two inside a phrase, where the pitch
    //    carries on across them.
    const int maxGap = juce::jmax (1, (int) std::lround (maxFilledGapSeconds / frameSeconds));

    for (int i = 1; i < numFrames; ++i)
    {
        if (p[(size_t) i] > 0.0f || p[(size_t) i - 1] <= 0.0f)
            continue;

        int end = i;

        while (end < numFrames && p[(size_t) end] <= 0.0f && end - i < maxGap)
            ++end;

        if (end < numFrames && p[(size_t) end] > 0.0f && std::abs (p[(size_t) end] - p[(size_t) i - 1]) < 0.5f)
            for (int k = i; k < end; ++k)
                p[(size_t) k] = juce::jmap ((float) (k - i + 1) / (float) (end - i + 1), p[(size_t) i - 1], p[(size_t) end]);

        i = end;
    }

    // 2. Smooth out single-frame octave slips and glitches: a 5-frame median
    //    inside each pitched run.
    {
        const auto raw = p;

        for (int i = 0; i < numFrames; ++i)
        {
            if (raw[(size_t) i] <= 0.0f)
                continue;

            std::vector<float> around;

            for (int k = juce::jmax (0, i - 2); k <= juce::jmin (numFrames - 1, i + 2); ++k)
                if (raw[(size_t) k] > 0.0f)
                    around.push_back (raw[(size_t) k]);

            p[(size_t) i] = median (around);
        }
    }

    // 3. Each pitched run is a phrase; split it into notes where the pitch
    //    moves away and stays away. The note's pitch so far is the median of
    //    its recent frames, leaving out its first third (where it's still arriving).
    const int splitFrames = juce::jmax (1, (int) std::ceil (splitSeconds / frameSeconds));
    std::vector<std::pair<int, int>> spans;   // [first, end) frames

    for (int runStart = 0; runStart < numFrames;)
    {
        if (p[(size_t) runStart] <= 0.0f)
        {
            ++runStart;
            continue;
        }

        int runEnd = runStart;

        while (runEnd < numFrames && p[(size_t) runEnd] > 0.0f)
            ++runEnd;

        int noteStart = runStart;

        for (int i = runStart + 1; i < runEnd; ++i)
        {
            // (At most the last second or so, so a long held note stays quick.)
            const int from = juce::jmax (noteStart + (i - noteStart) / 3, i - 200);
            const float ref = median (std::vector<float> (p.begin() + from, p.begin() + i));

            if (std::abs (p[(size_t) i] - ref) <= splitSemitones)
                continue;

            // Moved away: does it stay away?
            int away = i;

            while (away < runEnd && std::abs (p[(size_t) away] - ref) > splitSemitones)
                ++away;

            if (away - i >= splitFrames)
            {
                spans.push_back ({ noteStart, i });
                noteStart = i;
            }
            else
            {
                i = away;   // a blip (or vibrato's peak): skip past it
            }
        }

        spans.push_back ({ noteStart, runEnd });
        runStart = runEnd;
    }

    // 4. Short pieces: joined on to the note right after them (as its
    //    glide in), or before them, if there's no gap; otherwise dropped.
    //    A piece a little longer that's sliding all the way through (no
    //    steady pitch in it) and runs straight into the next note is that
    //    note's glide too.
    const int minFrames = juce::jmax (1, (int) std::ceil (minNoteSeconds / frameSeconds));
    const int maxGlideFrames = juce::jmax (minFrames, (int) std::ceil (maxGlideSeconds / frameSeconds));

    auto isGlide = [&] (size_t i)
    {
        const auto [first, end] = spans[i];

        if (end - first > maxGlideFrames || i + 1 >= spans.size() || spans[i + 1].first != end)
            return false;

        const auto [lowest, highest] = std::minmax_element (p.begin() + first, p.begin() + end);
        return *highest - *lowest > glideSemitones;
    };

    for (size_t i = 0; i < spans.size();)
    {
        if (spans[i].second - spans[i].first >= minFrames && ! isGlide (i))
        {
            ++i;
            continue;
        }

        if (i + 1 < spans.size() && spans[i + 1].first == spans[i].second)
            spans[i + 1].first = spans[i].first;
        else if (i > 0 && spans[i - 1].second == spans[i].first)
            spans[i - 1].second = spans[i].second;

        spans.erase (spans.begin() + (std::ptrdiff_t) i);
    }

    // 5. Each note's pitch: the median of its stable middle (leaving out
    //    the first fifth and the last sixth, where it arrives and leaves,
    //    then anything over half a semitone from the rest), and how far it
    //    wavers around that.
    for (auto [first, end] : spans)
    {
        const int count = end - first;
        const int from = first + count / 5, to = juce::jmax (from + 1, end - count / 6);
        std::vector<float> middle (p.begin() + from, p.begin() + to);
        const float rough = median (middle);
        std::vector<float> stable;

        for (auto v : middle)
            if (std::abs (v - rough) < 0.5f)
                stable.push_back (v);

        PitchNote note;
        note.firstFrame = first;
        note.numFrames = count;
        note.start = a.frameTime (first) - frameSeconds * 0.5;
        note.end = a.frameTime (end - 1) + frameSeconds * 0.5;
        note.centre = stable.empty() ? rough : median (stable);

        const auto& wavers = stable.empty() ? middle : stable;
        note.vibratoCents = 100.0f * 0.5f * (percentile (wavers, 0.95f) - percentile (wavers, 0.05f));
        a.notes.push_back (note);
    }

    // Frames that aren't in a note have no pitch (so the curves and the
    // correction leave them alone).
    std::vector<bool> inNote ((size_t) numFrames, false);

    for (auto& n : a.notes)
        std::fill (inNote.begin() + n.firstFrame, inNote.begin() + n.firstFrame + n.numFrames, true);

    // ...and the rest are rounded to the cache's precision (a hundredth of
    // a semitone), so an analysis read back from the cache is exactly the
    // one made now (the corrections' render is named after what they make).
    for (int i = 0; i < numFrames; ++i)
        p[(size_t) i] = inNote[(size_t) i] ? std::round (p[(size_t) i] * 100.0f) / 100.0f : 0.0f;
}

//==============================================================================
// The cache
namespace
{
    constexpr juce::uint32 cacheMagic = 0x4b50414b;   // "KAPK" little-endian: Ikaros pitch analysis
    constexpr int cacheVersion = 1;

    // FNV-1a, 64-bit: a quick, stable hash (the same on every system).
    juce::uint64 fnv1a (const void* data, size_t size, juce::uint64 hash)
    {
        auto* bytes = static_cast<const juce::uint8*> (data);

        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ bytes[i]) * 0x100000001b3ULL;

        return hash;
    }
}

juce::uint64 fingerprintOf (const juce::File& f)
{
    // The size and three 64 KB slices (start, middle, end): enough to tell
    // a re-recorded or replaced file, without reading the whole thing.
    const auto size = f.getSize();
    juce::uint64 hash = fnv1a (&size, sizeof (size), 0xcbf29ce484222325ULL);
    juce::FileInputStream in (f);

    if (! in.openedOk())
        return 0;

    juce::HeapBlock<char> chunk (65536);

    for (auto at : { (juce::int64) 0, size / 2, juce::jmax ((juce::int64) 0, size - 65536) })
    {
        in.setPosition (at);
        const auto got = in.read (chunk.getData(), 65536);
        hash = fnv1a (chunk.getData(), (size_t) juce::jmax (0, got), hash);
    }

    return hash;
}

bool writeCache (const juce::File& file, const PitchAnalysis& a, juce::uint64 fingerprint)
{
    // Written to a temporary file first, so a crash never leaves half a cache.
    juce::TemporaryFile temp (file);

    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk())
            return false;

        out.writeInt ((int) cacheMagic);
        out.writeInt (cacheVersion);
        out.writeInt64 ((juce::int64) fingerprint);
        out.writeDouble (a.sampleRate);
        out.writeInt (a.frameSize);
        out.writeInt (a.hop);
        out.writeBool (a.singleVoice);
        out.writeInt ((int) a.pitch.size());

        // Pitch as hundredths of a semitone (0 = none), voicing as 0..255.
        for (size_t i = 0; i < a.pitch.size(); ++i)
        {
            out.writeShort ((short) juce::jlimit (0, 32767, juce::roundToInt (a.pitch[i] * 100.0f)));
            out.writeByte ((char) juce::jlimit (0, 255, juce::roundToInt (a.voicing[i] * 255.0f)));
        }

        out.writeInt ((int) a.notes.size());

        for (auto& n : a.notes)
        {
            out.writeInt (n.firstFrame);
            out.writeInt (n.numFrames);
            out.writeDouble (n.start);
            out.writeDouble (n.end);
            out.writeFloat (n.centre);
            out.writeFloat (n.vibratoCents);
        }

        out.flush();

        if (out.getStatus().failed())
            return false;
    }

    return temp.overwriteTargetFileWithTemporary();
}

std::optional<PitchAnalysis> readCache (const juce::File& file, juce::uint64 fingerprint)
{
    juce::FileInputStream in (file);

    if (! in.openedOk() || (juce::uint32) in.readInt() != cacheMagic || in.readInt() != cacheVersion
         || (juce::uint64) in.readInt64() != fingerprint)
        return std::nullopt;

    PitchAnalysis a;
    a.sampleRate = in.readDouble();
    a.frameSize = in.readInt();
    a.hop = in.readInt();
    a.singleVoice = in.readBool();
    const int frames = in.readInt();

    if (a.sampleRate <= 0.0 || a.hop <= 0 || frames < 0 || (juce::int64) frames * 3 > in.getNumBytesRemaining())
        return std::nullopt;

    a.pitch.resize ((size_t) frames);
    a.voicing.resize ((size_t) frames);

    for (int i = 0; i < frames; ++i)
    {
        a.pitch[(size_t) i] = (float) in.readShort() / 100.0f;
        a.voicing[(size_t) i] = (float) (juce::uint8) in.readByte() / 255.0f;
    }

    const int count = in.readInt();

    if (count < 0 || (juce::int64) count * 32 > in.getNumBytesRemaining())
        return std::nullopt;

    for (int i = 0; i < count; ++i)
    {
        PitchNote n;
        n.firstFrame = in.readInt();
        n.numFrames = in.readInt();
        n.start = in.readDouble();
        n.end = in.readDouble();
        n.centre = in.readFloat();
        n.vibratoCents = in.readFloat();

        if (n.firstFrame < 0 || n.numFrames <= 0 || n.firstFrame + n.numFrames > frames)
            return std::nullopt;

        a.notes.push_back (n);
    }

    return a;
}

} // namespace ikaros::pitchanalysis
