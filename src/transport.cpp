#include "convex/transport.hpp"
#include "convex/platform/platform_detect.hpp"

#if defined(CONVEX_PLATFORM_POSIX)
#include "convex/platform/posix_transport.hpp"
#elif defined(CONVEX_PLATFORM_WINDOWS)
#include "convex/platform/win_transport.hpp"
#elif defined(CONVEX_PLATFORM_ESP32)
#include "convex/platform/esp32_transport.hpp"
#endif

namespace convex {

std::unique_ptr<ITransport> create_transport() {
#if defined(CONVEX_PLATFORM_POSIX)
    return std::make_unique<PosixTransport>();
#elif defined(CONVEX_PLATFORM_WINDOWS)
    return std::make_unique<WinTransport>();
#elif defined(CONVEX_PLATFORM_ESP32)
    return std::make_unique<Esp32Transport>();
#else
    #error "No transport implementation for this platform"
#endif
}

} // namespace convex
