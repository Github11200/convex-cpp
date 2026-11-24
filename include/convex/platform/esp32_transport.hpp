#pragma once

#include "../transport.hpp"
#include "platform_detect.hpp"

#if defined(CONVEX_PLATFORM_ESP32)

// ESP-IDF headers
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string>
#include <atomic>

namespace convex {

/**
 * ESP32-based WebSocket transport using ESP-IDF's esp_websocket_client.
 * 
 * This implementation uses:
 *   - esp_websocket_client for WebSocket
 *   - mbedTLS (built into ESP-IDF) for TLS
 *   - FreeRTOS for threading
 * 
 * Note: This transport is specifically designed for ESP-IDF environment.
 * It won't compile on other platforms.
 */
class Esp32Transport : public ITransport {
public:
    Esp32Transport();
    ~Esp32Transport() override;
    
    bool connect(const std::string& url, const std::string& client_id) override;
    bool send(const std::string& message) override;
    void close(const std::string& reason = "") override;
    ConnectionState state() const override;
    void set_callbacks(TransportCallbacks callbacks) override;
    void poll(int timeout_ms = 0) override;
    void run() override;
    
private:
    static void websocket_event_handler(
        void* handler_args,
        esp_event_base_t base,
        int32_t event_id,
        void* event_data
    );
    
    void handle_event(esp_websocket_event_data_t* data);
    
    esp_websocket_client_handle_t client_ = nullptr;
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    TransportCallbacks callbacks_;
    
    // For accumulating fragmented messages
    std::string message_buffer_;
};

} // namespace convex

#endif // CONVEX_PLATFORM_ESP32
