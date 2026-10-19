#include "platform/SystemAppearance.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_LINUX
 #include <systemd/sd-bus.h>
 #include <memory>
#endif

namespace ikaros::platform
{

#if JUCE_LINUX
namespace
{
    // The portal's answer: 1 = prefer dark, 2 = prefer light, 0 = no
    // preference. 0 too if there's no portal or it didn't answer.
    std::uint32_t readPortalColourScheme()
    {
        // One connection to the session bus, opened the first time and kept
        // (the theme is checked every few seconds while "Match system" is
        // chosen). The unique_ptr's deleter closes it when the app exits.
        static const std::unique_ptr<sd_bus, decltype (&sd_bus_flush_close_unref)> bus = []
        {
            sd_bus* b = nullptr;
            return std::unique_ptr<sd_bus, decltype (&sd_bus_flush_close_unref)> (sd_bus_open_user (&b) >= 0 ? b : nullptr,
                                                                                &sd_bus_flush_close_unref);
        }();

        if (bus == nullptr)
            return 0;

        // ReadOne is the current method; older portals only have Read, which
        // wraps the value in one more variant.
        for (const char* method : { "ReadOne", "Read" })
        {
            sd_bus_message* call = nullptr;

            if (sd_bus_message_new_method_call (bus.get(), &call, "org.freedesktop.portal.Desktop",
                                                "/org/freedesktop/portal/desktop",
                                                "org.freedesktop.portal.Settings", method) < 0)
                return 0;

            sd_bus_message_append (call, "ss", "org.freedesktop.appearance", "color-scheme");

            // A short timeout (in microseconds): this runs on the message
            // thread, and a missing portal mustn't freeze the window.
            sd_bus_message* reply = nullptr;
            sd_bus_error error = SD_BUS_ERROR_NULL;
            const int result = sd_bus_call (bus.get(), call, 250'000, &error, &reply);
            sd_bus_message_unref (call);
            sd_bus_error_free (&error);

            if (result < 0)
                continue;   // e.g. "no such method": try the older one

            // Step into the variant(s) until we reach the number.
            std::uint32_t value = 0;
            char type = 0;
            const char* contents = nullptr;

            while (sd_bus_message_peek_type (reply, &type, &contents) > 0 && type == SD_BUS_TYPE_VARIANT)
                if (sd_bus_message_enter_container (reply, SD_BUS_TYPE_VARIANT, contents) < 0)
                    break;

            if (type == SD_BUS_TYPE_UINT32)
                sd_bus_message_read_basic (reply, SD_BUS_TYPE_UINT32, &value);

            sd_bus_message_unref (reply);
            return value;
        }

        return 0;
    }
}
#endif

bool systemPrefersDark()
{
   #if JUCE_LINUX
    switch (readPortalColourScheme())
    {
        case 1:  return true;
        case 2:  return false;
        default: break;   // no preference, or no portal: ask JUCE
    }
   #endif

    return juce::Desktop::getInstance().isDarkModeActive();
}

} // namespace ikaros::platform
