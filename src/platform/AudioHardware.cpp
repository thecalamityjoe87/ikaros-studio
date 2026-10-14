#include "platform/AudioHardware.h"

#if JUCE_LINUX
 #include <alsa/asoundlib.h>
#endif

namespace ikaros::platform
{

bool refreshAudioDeviceLists (juce::AudioDeviceManager& manager, HardwareWatch& watch)
{
   #if JUCE_LINUX
    const auto cards = juce::File ("/proc/asound/cards").loadFileAsString();

    // The first call only notes what's there.
    if (watch.cards.isEmpty())
    {
        watch.cards = cards;
        return false;
    }

    // A card is listed in /proc before its device files are usable, so one scan can miss it: scan again for a while.
    if (cards != watch.cards)
    {
        watch.cards = cards;
        watch.scansLeft = 6;
    }

    if (watch.scansLeft == 0)
        return false;

    --watch.scansLeft;

    juce::AudioIODeviceType* stale = nullptr;

    for (auto* type : manager.getAvailableDeviceTypes())
        if (type->getTypeName() == "ALSA")
            stale = type;

    if (stale == nullptr)
        return false;

    // JUCE's ALSA list never scans twice, so a new one stands in for the old.
    std::unique_ptr<juce::AudioIODeviceType> fresh (juce::AudioIODeviceType::createAudioIODeviceType_ALSA());

    if (fresh == nullptr)
        return false;

    // ALSA caches its configuration, so a card plugged in since wouldn't be listed.
    snd_config_update_free_global();
    fresh->scanForDevices();

    if (fresh->getDeviceNames (true) == stale->getDeviceNames (true)
         && fresh->getDeviceNames (false) == stale->getDeviceNames (false))
        return false;

    manager.removeAudioDeviceType (stale);
    manager.addAudioDeviceType (std::move (fresh));
    manager.sendChangeMessage();   // so an open device picker updates too
    return true;
   #else
    juce::ignoreUnused (manager, watch);
    return false;
   #endif
}

} // namespace ikaros::platform
