#pragma once

#include "../transport.hpp"
#include "platform_detect.hpp"

#if defined(CONVEX_PLATFORM_WINDOWS)

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

namespace convex {

/**
 * Windows-based WebSocket transport using WinHTTP.
 * 
 * This implementation uses:
 *   - WinHTTP for WebSocket connections
 *   - SChannel (via WinHTTP) for TLS
 *   - Win32 threads
 * 
 * Note: Requires Windows 8 or later for WebSocket support.
 */
class WinTransport : public ITransport {
public:
    WinTransport();
    ~WinTransport() override;
    
    bool connect(const std::string& url, const std::string& client_id) override;
    bool send(const std::string& message) override;
    void close(const std::string& reason = "") override;
    ConnectionState state() const override;
    void set_callbacks(TransportCallbacks callbacks) override;
    void poll(int timeout_ms = 0) override;
    void run() override;
    
private:
    bool perform_handshake(const std::string& host, int port,
                           const std::string& path, const std::string& client_id);
    
    // WinHTTP's HINTERNET is a void pointer. Keep SDK headers private.
    void* session_ = nullptr;
    void* connection_ = nullptr;
    void* websocket_ = nullptr;
    
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    TransportCallbacks callbacks_;
    
    std::vector<uint8_t> read_buffer_;
    std::string partial_message_;
    std::mutex send_mutex_;
};

} // namespace convex

#endif // CONVEX_PLATFORM_WINDOWS
