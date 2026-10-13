#include "platform/AudioHardware.h"

namespace ikaros::platform
{

bool refreshAudioDeviceLists (juce::AudioDeviceManager& manager, juce::String& lastSeen)
{
   #if JUCE_LINUX
    const auto cards = juce::File ("/proc/asound/cards").loadFileAsString();

    const auto before = std::exchange (lastSeen, cards);

    // The first call only notes what's there.
    if (before.isEmpty() || before == cards)
        return false;

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

    fresh->scanForDevices();
    manager.removeAudioDeviceType (stale);
    manager.addAudioDeviceType (std::move (fresh));
    manager.sendChangeMessage();   // so an open device picker updates too
    return true;
   #else
    juce::ignoreUnused (manager, lastSeen);
    return false;
   #endif
}

} // namespace ikaros::platform
