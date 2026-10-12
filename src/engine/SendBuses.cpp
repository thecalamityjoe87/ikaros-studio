#include "engine/SendBuses.h"
#include "engine/AudioEngine.h"
#include "engine/TrackHelpers.h"

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

namespace
{
    // A send at "full" is 0 dB. The send plugin stores its level as a fader
    // position, where 0 dB sits a little below the top.
    float fullLevelPosition()   { return te::decibelsToVolumeFaderPosition (0.0f); }

    te::AuxSendPlugin* findSend (te::AudioTrack& t, int bus)
    {
        for (auto* p : t.pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
                if (send->getBusNumber() == bus)
                    return send;

        return nullptr;
    }

    te::AudioTrack* findBusTrack (te::Edit& edit, int bus)
    {
        for (auto* t : te::getAudioTracks (edit))
            if (isBusTrack (*t) && (int) t->state.getProperty (busProp) == bus)
                return t;

        return nullptr;
    }

    // The hidden track that returns a bus: Aux Return -> the effect, fully wet.
    void createBusTrack (te::Edit& edit, int bus)
    {
        auto all = te::getAudioTracks (edit);
        auto track = edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, all.isEmpty() ? nullptr : all.getLast()), nullptr);

        if (track == nullptr)
            return;

        auto* um = &edit.getUndoManager();
        track->state.setProperty (busProp, bus, um);
        track->setName (SendBuses::getBusName (bus) + " Bus");
        track->setSoloIsolate (true);   // soloing a track mustn't silence its reverb
        edit.setAuxBusName (bus, SendBuses::getBusName (bus));

        auto& cache = edit.getPluginCache();
        auto auxReturn = cache.createNewPlugin (te::createValueTree (te::IDs::PLUGIN,
                                                                     te::IDs::type, te::AuxReturnPlugin::xmlTypeName,
                                                                     te::IDs::busNum, bus));

        // The effect's settings go in its state before it's created, so they
        // aren't separate undo steps.
        auto effectState = bus == SendBuses::reverb
            ? te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::ReverbPlugin::xmlTypeName,
                                   te::IDs::roomSize, 0.7f, te::IDs::damp, 0.4f, te::IDs::dry, 0.0f)
            : te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::DelayPlugin::xmlTypeName,
                                   te::IDs::mix, 1.0f, te::IDs::feedback, -8.0f, te::IDs::length, 375);
        auto effect = cache.createNewPlugin (effectState);

        if (auxReturn != nullptr)
            track->pluginList.insertPlugin (auxReturn, 0, nullptr);

        if (effect != nullptr)
            track->pluginList.insertPlugin (effect, 1, nullptr);
    }
}

SendBuses::SendBuses (AudioEngine& o) : owner (o)
{
}

juce::String SendBuses::getBusName (int bus)
{
    return bus == reverb ? "Reverb" : "Delay";
}

float SendBuses::getSendLevel (TrackId id, int bus) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* send = findSend (*t, bus))
            return juce::jlimit (0.0f, 1.0f, send->gain->getCurrentValue() / fullLevelPosition());

    return 0.0f;
}

void SendBuses::setSendLevel (TrackId id, int bus, float level)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr || ! juce::isPositiveAndBelow (bus, numBuses))
        return;

    level = juce::jlimit (0.0f, 1.0f, level);
    auto* send = findSend (*t, bus);

    if (send == nullptr)
    {
        if (level <= 0.0f)
            return;

        if (findBusTrack (*edit, bus) == nullptr)
            createBusTrack (*edit, bus);

        // Created silent, then turned up below, so undo has a value to go back to.
        auto plugin = edit->getPluginCache().createNewPlugin (te::createValueTree (te::IDs::PLUGIN,
                                                                                   te::IDs::type, te::AuxSendPlugin::xmlTypeName,
                                                                                   te::IDs::busNum, bus,
                                                                                   te::IDs::auxSendSliderPos, 0.0f));
        send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());

        if (send == nullptr)
            return;

        // After the volume plugin: post-fader.
        auto* vol = t->getVolumePlugin();
        t->pluginList.insertPlugin (plugin, vol != nullptr ? t->pluginList.indexOf (vol) + 1 : -1, nullptr);
    }

    setParameterUndoably (*send, *send->gain, te::IDs::auxSendSliderPos, level * fullLevelPosition());
}

} // namespace ikaros
