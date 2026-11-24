#pragma once

/**
 * Convex C++ Client Library
 * 
 * A cross-platform C++ client for Convex (https://convex.dev).
 * 
 * Supported platforms:
 *   - Linux (POSIX + OpenSSL)
 *   - macOS (POSIX + OpenSSL)  
 *   - Windows (WinHTTP)
 *   - ESP32 (ESP-IDF)
 * 
 * Basic usage:
 * ```cpp
 * #include <convex/convex.hpp>
 * 
 * int main() {
 *     convex::ConvexClient client("https://your-app.convex.cloud");
 *     
 *     // Subscribe to a query
 *     auto sub = client.subscribe("messages:list", {}, [](const auto& result) {
 *         if (result.is_success()) {
 *             std::cout << result.value.to_string() << std::endl;
 *         }
 *     });
 *     
 *     // Call a mutation
 *     client.mutation("messages:send", {
 *         {"body", "Hello from C++!"},
 *         {"author", "C++ Client"}
 *     });
 *     
 *     // Run until stopped
 *     client.run();
 * }
 * ```
 */

// Core types
#include "value.hpp"
#include "error.hpp"

// Validation
#include "identifier.hpp"
#include "udf_path.hpp"

// Protocol types (usually not needed directly)
#include "protocol.hpp"

// Transport interface (for custom implementations)
#include "transport.hpp"

// Client classes
#include "base_client.hpp"
#include "client.hpp"

// Testing utilities (optional)
#if defined(CONVEX_INCLUDE_TESTING)
#include "testing/mock_transport.hpp"
#endif

namespace convex {

/**
 * Library version
 */
constexpr const char* VERSION = "0.1.0";

/**
 * Library name for client identification
 */
constexpr const char* CLIENT_NAME = "cpp-convex";

} // namespace convex
