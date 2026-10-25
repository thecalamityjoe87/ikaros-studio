#include "platform/SystemAppearance.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_LINUX
 #include <systemd/sd-bus.h>
 #include <cstring>
#endif

namespace ikaros::platform
{

#if JUCE_LINUX
namespace
{
    constexpr const char* portalService  = "org.freedesktop.portal.Desktop";
    constexpr const char* portalPath     = "/org/freedesktop/portal/desktop";
    constexpr const char* portalSettings = "org.freedesktop.portal.Settings";
    constexpr const char* appearance     = "org.freedesktop.appearance";
    constexpr const char* colourScheme   = "color-scheme";

    /** Steps into the variant(s) at the message's read position and reads
        the number inside: ReadOne's reply and the signal wrap it in one
        variant, the older Read's reply in two. 0 if it isn't a number. */
    std::uint32_t readSchemeValue (sd_bus_message* m)
    {
        std::uint32_t value = 0;
        char type = 0;
        const char* contents = nullptr;

        while (sd_bus_message_peek_type (m, &type, &contents) > 0 && type == SD_BUS_TYPE_VARIANT)
            if (sd_bus_message_enter_container (m, SD_BUS_TYPE_VARIANT, contents) < 0)
                break;

        if (type == SD_BUS_TYPE_UINT32)
            sd_bus_message_read_basic (m, SD_BUS_TYPE_UINT32, &value);

        return value;
    }
}

/*  The portal's colour scheme, kept current by its SettingChanged signal.

    The connection is ours alone and is only ever touched on the message
    thread: JUCE's event loop watches its socket, and when something
    arrives calls processPending() there, which reads it without waiting.
*/
struct SystemAppearance::Portal
{
    explicit Portal (std::function<void()> callback)  : onChange (std::move (callback))
    {
        if (sd_bus_open_user (&bus) < 0)
        {
            bus = nullptr;
            return;
        }

        // Subscribe before reading, so a change in between isn't missed. The
        // request goes out with the read below; the bus's answer to it is
        // handled later, like a signal. (If the bus refuses it, sd-bus's
        // default handler closes the connection, and processPending() lets
        // it go.)
        if (sd_bus_match_signal_async (bus, &match, portalService, portalPath, portalSettings,
                                       "SettingChanged", &Portal::settingChanged, nullptr, this) < 0)
        {
            close();
            return;
        }

        scheme = readNow();

        // While sd_bus_call waited for its reply it may have read other
        // messages (the subscription's answer, a signal) off the socket into
        // sd-bus's queue, where the socket watch below won't see them.
        processPending();

        if (bus != nullptr)
        {
            watchedFd = sd_bus_get_fd (bus);
            juce::LinuxEventLoop::registerFdCallback (watchedFd, [this] (int) { processPending(); });
        }

        started = true;
    }

    ~Portal()   { close(); }

    /** Asks the portal, waiting for the answer: done once, at startup. The
        portal's answer: 1 = prefer dark, 2 = prefer light, 0 = no
        preference. 0 too if there's no portal or it didn't answer. */
    std::uint32_t readNow()
    {
        // ReadOne is the current method; older portals only have Read.
        for (const char* method : { "ReadOne", "Read" })
        {
            sd_bus_message* call = nullptr;

            if (sd_bus_message_new_method_call (bus, &call, portalService, portalPath, portalSettings, method) < 0)
                return 0;

            sd_bus_message_append (call, "ss", appearance, colourScheme);

            // A short timeout (in microseconds): this runs on the message
            // thread, and a missing portal mustn't hold up the window.
            sd_bus_message* reply = nullptr;
            sd_bus_error error = SD_BUS_ERROR_NULL;
            const int result = sd_bus_call (bus, call, 250'000, &error, &reply);
            sd_bus_message_unref (call);
            sd_bus_error_free (&error);

            if (result < 0)
                continue;   // e.g. "no such method": try the older one

            const auto value = readSchemeValue (reply);
            sd_bus_message_unref (reply);
            return value;
        }

        return 0;
    }

    /** Handles whatever sd-bus has read or can read now, without waiting. */
    void processPending()
    {
        int result = 0;

        while ((result = sd_bus_process (bus, nullptr)) > 0)
        {
        }

        // The connection has gone (e.g. the session bus quit): stop watching,
        // or its closed socket would wake the event loop over and over. The
        // last scheme we had stays.
        if (result < 0)
            close();
    }

    // The signal's arguments: the setting's namespace, its key, and its new
    // value in a variant.
    static int settingChanged (sd_bus_message* m, void* userData, sd_bus_error*)
    {
        auto& self = *static_cast<Portal*> (userData);
        const char* ns = nullptr;
        const char* key = nullptr;

        if (sd_bus_message_read (m, "ss", &ns, &key) < 0
             || std::strcmp (ns, appearance) != 0 || std::strcmp (key, colourScheme) != 0)
            return 0;

        const auto value = readSchemeValue (m);

        if (value == self.scheme)
            return 0;

        self.scheme = value;

        if (self.started)
            self.onChange();

        return 0;
    }

    void close()
    {
        // Safe from inside the socket's own callback: JUCE keeps the callback
        // alive until it returns.
        if (watchedFd >= 0)
            juce::LinuxEventLoop::unregisterFdCallback (std::exchange (watchedFd, -1));

        match = sd_bus_slot_unref (match);

        if (bus != nullptr)
            bus = sd_bus_flush_close_unref (bus);
    }

    std::function<void()> onChange;
    sd_bus* bus = nullptr;
    sd_bus_slot* match = nullptr;
    int watchedFd = -1;
    std::uint32_t scheme = 0;
    bool started = false;   // no onChange while we're still being made
};
#else
struct SystemAppearance::Portal {};
#endif

SystemAppearance::SystemAppearance (std::function<void()> onChange)
{
   #if JUCE_LINUX
    portal = std::make_unique<Portal> (std::move (onChange));
   #else
    juce::ignoreUnused (onChange);
   #endif
}

SystemAppearance::~SystemAppearance() = default;

bool SystemAppearance::prefersDark() const
{
   #if JUCE_LINUX
    switch (portal->scheme)
    {
        case 1:  return true;
        case 2:  return false;
        default: break;   // no preference, or no portal: ask JUCE
    }
   #endif

    return juce::Desktop::getInstance().isDarkModeActive();
}

} // namespace ikaros::platform
