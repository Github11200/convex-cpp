#pragma once

#include "../transport.hpp"
#include "platform_detect.hpp"

#if defined(CONVEX_PLATFORM_POSIX)

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

namespace convex {

/**
 * POSIX-based WebSocket transport using raw sockets + OpenSSL.
 * 
 * This implementation works on Linux and macOS.
 * It uses BSD sockets for networking and OpenSSL for TLS.
 * 
 * Dependencies:
 *   - OpenSSL (libssl, libcrypto)
 *   - pthreads
 */
class PosixTransport : public ITransport {
public:
    PosixTransport();
    ~PosixTransport() override;
    
    bool connect(const std::string& url, const std::string& client_id) override;
    bool send(const std::string& message) override;
    void close(const std::string& reason = "") override;
    ConnectionState state() const override;
    void set_callbacks(TransportCallbacks callbacks) override;
    void poll(int timeout_ms = 0) override;
    void run() override;
    
private:
    bool perform_handshake(const std::string& host, const std::string& path,
                           const std::string& client_id);
    bool read_http_response();
    bool send_raw(const uint8_t* data, size_t len);
    bool receive_raw(uint8_t* buffer, size_t len, size_t& bytes_read);
    void send_websocket_frame(uint8_t opcode, const std::string& payload);
    bool receive_websocket_frame();
    void send_close_frame(uint16_t code = 1000, const std::string& reason = "");
    void send_pong(const std::string& payload);
    void parse_url(const std::string& url, std::string& host, int& port,
                   std::string& path, bool& use_tls);
    
    // Socket and SSL handles
    int socket_fd_ = -1;
    void* ssl_ctx_ = nullptr;  // SSL_CTX*
    void* ssl_ = nullptr;       // SSL*
    
    // State
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    bool use_tls_ = true;
    
    // Callbacks
    TransportCallbacks callbacks_;
    
    // Buffers
    std::vector<uint8_t> read_buffer_;
    std::queue<std::string> pending_messages_;
    std::mutex send_mutex_;
    
    // Partial frame state
    std::string partial_message_;
    bool in_fragmented_message_ = false;
};

} // namespace convex

#endif // CONVEX_PLATFORM_POSIX
