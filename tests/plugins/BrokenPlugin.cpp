// A "plug-in" that crashes (or hangs) as soon as a host loads it, to test
// that a bad plug-in can't take the app down during a scan. It isn't a real
// VST3: the code below runs when the library is loaded, before the host gets
// as far as asking what's inside.
//
// Built twice by tests/plugins/CMakeLists.txt: IKAROS_HANG = 0 crashes,
// IKAROS_HANG = 1 never returns.

#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{
    struct LoadTimeTrap
    {
        LoadTimeTrap()
        {
           #if IKAROS_HANG
            for (;;)
                std::this_thread::sleep_for (std::chrono::seconds (1));
           #else
            std::abort();
           #endif
        }
    };

    // A global object's constructor runs when the shared library is loaded.
    const LoadTimeTrap trap;
}

// The function a VST3 host looks for. Never reached.
extern "C" __attribute__ ((visibility ("default"))) void* GetPluginFactory()   { return nullptr; }
