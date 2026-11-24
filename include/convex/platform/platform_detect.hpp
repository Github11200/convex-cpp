#pragma once

// Platform detection macros

#if defined(ESP_PLATFORM) || defined(ESP32)
    #define CONVEX_PLATFORM_ESP32 1
    #define CONVEX_PLATFORM_NAME "ESP32"
#elif defined(_WIN32) || defined(_WIN64)
    #define CONVEX_PLATFORM_WINDOWS 1
    #define CONVEX_PLATFORM_NAME "Windows"
#elif defined(__APPLE__) && defined(__MACH__)
    #define CONVEX_PLATFORM_MACOS 1
    #define CONVEX_PLATFORM_POSIX 1
    #define CONVEX_PLATFORM_NAME "macOS"
#elif defined(__linux__)
    #define CONVEX_PLATFORM_LINUX 1
    #define CONVEX_PLATFORM_POSIX 1
    #define CONVEX_PLATFORM_NAME "Linux"
#elif defined(__unix__)
    #define CONVEX_PLATFORM_UNIX 1
    #define CONVEX_PLATFORM_POSIX 1
    #define CONVEX_PLATFORM_NAME "Unix"
#else
    #error "Unsupported platform"
#endif

// Threading model
#if defined(CONVEX_PLATFORM_ESP32)
    #define CONVEX_THREADING_FREERTOS 1
#elif defined(CONVEX_PLATFORM_WINDOWS)
    #define CONVEX_THREADING_WIN32 1
#else
    #define CONVEX_THREADING_PTHREADS 1
#endif

// C++ standard library availability
#if defined(CONVEX_PLATFORM_ESP32)
    // ESP-IDF has limited C++ STL support
    #define CONVEX_HAS_STD_THREAD 0
    #define CONVEX_HAS_STD_MUTEX 1
    #define CONVEX_HAS_STD_OPTIONAL 1
#else
    #define CONVEX_HAS_STD_THREAD 1
    #define CONVEX_HAS_STD_MUTEX 1
    #define CONVEX_HAS_STD_OPTIONAL 1
#endif
