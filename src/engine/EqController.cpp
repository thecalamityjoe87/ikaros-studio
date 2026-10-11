#include "engine/EqController.h"
#include "engine/AudioEngine.h"
#include "engine/TrackHelpers.h"

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

namespace
{
    // The EQ plugin on a track (or the master), if it has one yet.
    te::EqualiserPlugin* findEq (te::PluginList& list)
    {
        for (auto* p : list)
            if (isEqPlugin (*p))
                if (auto* eq = dynamic_cast<te::EqualiserPlugin*> (p))
                    return eq;

        return nullptr;
    }

    // Each band's three parameters, and the properties Tracktion stores them
    // in. Bands go low shelf, low mid, high mid, high shelf.
    struct BandParams
    {
        te::AutomatableParameter* frequency;
        te::AutomatableParameter* gain;
        te::AutomatableParameter* q;
        juce::Identifier frequencyId, gainId, qId;
    };

    BandParams bandParams (te::EqualiserPlugin& eq, int band)
    {
        switch (band)
        {
            case 0:  return { eq.loFreq.get(),   eq.loGain.get(),   eq.loQ.get(),   te::IDs::loFreq,   te::IDs::loGain,   te::IDs::loQ };
            case 1:  return { eq.midFreq1.get(), eq.midGain1.get(), eq.midQ1.get(), te::IDs::midFreq1, te::IDs::midGain1, te::IDs::midQ1 };
            case 2:  return { eq.midFreq2.get(), eq.midGain2.get(), eq.midQ2.get(), te::IDs::midFreq2, te::IDs::midGain2, te::IDs::midQ2 };
            default: return { eq.hiFreq.get(),   eq.hiGain.get(),   eq.hiQ.get(),   te::IDs::hiFreq,   te::IDs::hiGain,   te::IDs::hiQ };
        }
    }

    // Where a new EQ goes: after a track's effects, just before its volume
    // plugin (Tracktion keeps volume and meter at the end); at the end of
    // the master's effects.
    int insertIndex (te::PluginList& list)
    {
        for (int i = 0; i < list.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) != nullptr)
                return i;

        return list.size();
    }

    // The EQ, added (as part of the current undo step) if it isn't there yet.
    te::EqualiserPlugin* findOrAddEq (te::Edit& edit, te::PluginList& list)
    {
        if (auto* eq = findEq (list))
            return eq;

        // The starting settings go into its state before it's created, so
        // making it isn't a pile of separate undo steps.
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::EqualiserPlugin::xmlTypeName);
        state.setProperty (eqProp, true, nullptr);

        const auto defaults = EqController::getDefaults();

        for (int b = 0; b < EqSettings::numBands; ++b)
        {
            const auto& band = defaults.bands[(size_t) b];
            const juce::Identifier ids[4][3] { { te::IDs::loFreq,   te::IDs::loGain,   te::IDs::loQ },
                                               { te::IDs::midFreq1, te::IDs::midGain1, te::IDs::midQ1 },
                                               { te::IDs::midFreq2, te::IDs::midGain2, te::IDs::midQ2 },
                                               { te::IDs::hiFreq,   te::IDs::hiGain,   te::IDs::hiQ } };
            state.setProperty (ids[b][0], band.frequencyHz, nullptr);
            state.setProperty (ids[b][1], band.gainDb, nullptr);
            state.setProperty (ids[b][2], band.q, nullptr);
        }

        auto plugin = edit.getPluginCache().createNewPlugin (state);
        list.insertPlugin (plugin, insertIndex (list), nullptr);
        return findEq (list);
    }

    // The settings a plugin holds (or the defaults without one).
    EqSettings readSettings (te::EqualiserPlugin* eq)
    {
        auto settings = EqController::getDefaults();

        if (eq == nullptr)
            return settings;

        for (int b = 0; b < EqSettings::numBands; ++b)
        {
            auto params = bandParams (*eq, b);
            auto& band = settings.bands[(size_t) b];
            band.frequencyHz = params.frequency->getCurrentValue();
            band.gainDb = params.gain->getCurrentValue();
            band.q = params.q->getCurrentValue();
        }

        settings.enabled = eq->isEnabled();
        return settings;
    }
}

//==============================================================================
EqController::EqController (AudioEngine& o) : owner (o)
{
}

EqSettings EqController::getDefaults()
{
    // Gentle, musical starting points: a shelf below the bass and above the
    // vocals' presence range, and two wide bands in between.
    EqSettings s;
    s.bands = {{ { "Low",      100.0f,   0.0f, 0.7f },
                 { "Low Mid",  400.0f,   0.0f, 0.7f },
                 { "High Mid", 2500.0f,  0.0f, 0.7f },
                 { "High",     10000.0f, 0.0f, 0.7f } }};
    return s;
}

EqSettings EqController::getEq (TrackId id) const
{
    auto* list = findPluginList (owner.getEdit(), id);
    return readSettings (list != nullptr ? findEq (*list) : nullptr);
}

void EqController::setBand (TrackId id, int band, float frequencyHz, float gainDb, float q)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);

    if (list == nullptr || ! juce::isPositiveAndBelow (band, EqSettings::numBands))
        return;

    auto* eq = findOrAddEq (*edit, *list);

    if (eq == nullptr)
        return;

    auto params = bandParams (*eq, band);
    setParameterUndoably (*eq, *params.frequency, params.frequencyId, juce::jlimit (minFrequency, maxFrequency, frequencyHz));
    setParameterUndoably (*eq, *params.gain, params.gainId, juce::jlimit (minGain, maxGain, gainDb));
    setParameterUndoably (*eq, *params.q, params.qId, juce::jlimit (minQ, maxQ, q));
}

void EqController::setEnabled (TrackId id, bool enabled)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);

    if (list == nullptr || getEq (id).enabled == enabled)
        return;

    owner.beginUndoStep (enabled ? "Turn EQ On" : "Turn EQ Off");

    if (auto* eq = findOrAddEq (*edit, *list))
        eq->setEnabled (enabled);
}

void EqController::reset (TrackId id)
{
    auto* list = findPluginList (owner.getEdit(), id);
    auto* eq = list != nullptr ? findEq (*list) : nullptr;

    if (eq == nullptr)
        return;   // never changed: already flat

    owner.beginUndoStep ("Reset EQ");
    const auto defaults = getDefaults();

    for (int b = 0; b < EqSettings::numBands; ++b)
    {
        const auto& band = defaults.bands[(size_t) b];
        setBand (id, b, band.frequencyHz, band.gainDb, band.q);
    }
}

std::vector<float> EqController::getResponse (TrackId id, const std::vector<float>& frequencies) const
{
    std::vector<float> result (frequencies.size(), 0.0f);
    const auto settings = getEq (id);

    if (! settings.enabled)
        return result;

    // The same filters Tracktion's equaliser uses (low shelf, two peaks,
    // high shelf). Each one's effect, in dB, adds up. The sample rate only
    // matters near the top of the range, so a typical one does.
    constexpr double sampleRate = 48000.0;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    for (int b = 0; b < EqSettings::numBands; ++b)
    {
        const auto& band = settings.bands[(size_t) b];

        if (std::abs (band.gainDb) < 0.01f)
            continue;   // a flat band changes nothing

        const auto gain = juce::Decibels::decibelsToGain (band.gainDb);
        auto coefficients = b == 0 ? Coefficients::makeLowShelf (sampleRate, band.frequencyHz, band.q, gain)
                          : b == 3 ? Coefficients::makeHighShelf (sampleRate, band.frequencyHz, band.q, gain)
                                   : Coefficients::makePeakFilter (sampleRate, band.frequencyHz, band.q, gain);

        for (size_t i = 0; i < frequencies.size(); ++i)
            result[i] += (float) juce::Decibels::gainToDecibels (coefficients->getMagnitudeForFrequency (frequencies[i], sampleRate));
    }

    return result;
}

} // namespace ikaros
