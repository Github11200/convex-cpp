# Convex C++ Client

A cross-platform C++ client for [Convex](https://convex.dev), ported from the official Rust client.

## Supported Platforms

| Platform | Transport | Threading |
|----------|-----------|-----------|
| Linux | POSIX sockets + OpenSSL | pthreads |
| macOS | POSIX sockets + OpenSSL | pthreads |
| Windows | Winsock + SChannel | Win32 threads |
| ESP32 | esp_websocket_client | FreeRTOS |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ConvexClient                           │
│              (High-level async API)                         │
├─────────────────────────────────────────────────────────────┤
│                    BaseConvexClient                         │
│         (Synchronous state machine - portable)              │
├─────────────────────────────────────────────────────────────┤
│                    ITransport                               │
│              (Abstract WebSocket interface)                 │
├──────────────┬──────────────┬──────────────┬────────────────┤
│ PosixTransport│ WinTransport │Esp32Transport│ MockTransport │
│ (Linux/macOS) │  (Windows)   │   (ESP-IDF)  │   (Testing)   │
└──────────────┴──────────────┴──────────────┴────────────────┘
```

## Directory Structure

```
convex-cplusplus/
├── include/convex/
│   ├── convex.hpp              # Main include header
│   ├── value.hpp               # Value type (null, int64, float64, string, etc.)
│   ├── protocol.hpp            # ClientMessage, ServerMessage types
│   ├── base_client.hpp         # Synchronous state machine
│   ├── client.hpp              # High-level ConvexClient
│   ├── transport.hpp           # ITransport interface
│   ├── error.hpp               # Error types
│   ├── json.hpp                # JSON serialization helpers
│   └── platform/
│       ├── platform_detect.hpp # Platform detection macros
│       ├── posix_transport.hpp # Linux/macOS implementation
│       ├── win_transport.hpp   # Windows implementation
│       └── esp32_transport.hpp # ESP32 implementation
├── src/
│   ├── value.cpp
│   ├── protocol.cpp
│   ├── base_client.cpp
│   ├── client.cpp
│   └── platform/
│       ├── posix_transport.cpp
│       ├── win_transport.cpp
│       └── esp32_transport.cpp
├── third_party/
│   └── nlohmann/json.hpp       # JSON library (header-only)
├── examples/
│   ├── simple_subscribe.cpp
│   ├── mutation_example.cpp
│   └── esp32/
│       └── main.cpp
├── CMakeLists.txt
└── README.md
```

## Building

### Quick Start (Linux/macOS)

```bash
# Build library and examples
./build.sh

# Build and run tests
./build.sh test

# Release build (optimized)
./build.sh release

# Clean build directory
./build.sh clean
```

### Manual Build (Linux/macOS/Windows)

```bash
mkdir build && cd build
cmake -DCONVEX_BUILD_TESTS=ON ..
make -j$(nproc)

# Run tests
./convex_tests
```

### ESP32 (ESP-IDF)

See [docs/ESP32_INTEGRATION.md](docs/ESP32_INTEGRATION.md) for detailed instructions.

Quick setup:
```bash
# Copy to your project's components folder
cp -r /path/to/convex-cplusplus your-project/components/convex

# Build
idf.py build
```

## Usage

```cpp
#include <convex/convex.hpp>

int main() {
    convex::ConvexClient client("https://your-app.convex.cloud");
    
    // Subscribe to a query
    auto sub = client.subscribe("messages:list", {});
    
    // Receive updates
    sub.on_update([](const convex::Value& result) {
        std::cout << "Got update: " << result.to_string() << std::endl;
    });
    
    // Call a mutation
    auto result = client.mutation("messages:send", {
        {"body", "Hello from C++!"},
        {"author", "C++ Client"}
    });
    
    // Run event loop
    client.run();
    
    return 0;
}
```

## Dependencies

- **nlohmann/json** (included, header-only)
- **OpenSSL** (Linux/macOS) or **SChannel** (Windows) for TLS
- **ESP-IDF** (ESP32 only)

## Ported From

This is a port of [convex-rs](https://github.com/get-convex/convex-rs), the official Rust client.
