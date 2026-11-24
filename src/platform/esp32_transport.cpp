#include "convex/platform/esp32_transport.hpp"

#if defined(CONVEX_PLATFORM_ESP32)

#include "esp_log.h"
#include <cstring>

static const char* TAG = "convex_transport";

namespace convex {

Esp32Transport::Esp32Transport() = default;

Esp32Transport::~Esp32Transport() {
    close();
}

void Esp32Transport::websocket_event_handler(
    void* handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void* event_data
) {
    auto* transport = static_cast<Esp32Transport*>(handler_args);
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
    transport->handle_event(data);
}

void Esp32Transport::handle_event(esp_websocket_event_data_t* data) {
    switch (data->op_code) {
        case 0:  // Continuation
        case 1:  // Text
        case 2:  // Binary
            // Accumulate data
            if (data->data_len > 0 && data->data_ptr) {
                message_buffer_.append(data->data_ptr, data->data_len);
            }
            
            // Check if this is the final fragment
            if (data->fin) {
                if (callbacks_.on_message && !message_buffer_.empty()) {
                    callbacks_.on_message(message_buffer_);
                }
                message_buffer_.clear();
            }
            break;
            
        case 8:  // Close
            state_ = ConnectionState::Disconnected;
            if (callbacks_.on_close) {
                std::string reason;
                if (data->data_len > 2 && data->data_ptr) {
                    reason = std::string(data->data_ptr + 2, data->data_len - 2);
                }
                callbacks_.on_close(reason);
            }
            break;
            
        case 9:  // Ping - handled automatically by esp_websocket_client
        case 10: // Pong
            break;
    }
}

bool Esp32Transport::connect(const std::string& url, const std::string& client_id) {
    state_ = ConnectionState::Connecting;
    
    esp_websocket_client_config_t config = {};
    config.uri = url.c_str();
    config.task_stack_size = 8192;
    config.buffer_size = 4096;
    
    // Set up headers for client ID
    char headers[256];
    snprintf(headers, sizeof(headers), "Convex-Client: %s\r\n", client_id.c_str());
    config.headers = headers;
    
    client_ = esp_websocket_client_init(&config);
    if (!client_) {
        ESP_LOGE(TAG, "Failed to init websocket client");
        state_ = ConnectionState::Disconnected;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to init websocket client");
        }
        return false;
    }
    
    // Register event handler
    esp_websocket_register_events(
        client_,
        WEBSOCKET_EVENT_ANY,
        websocket_event_handler,
        this
    );
    
    esp_err_t err = esp_websocket_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start websocket client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
        state_ = ConnectionState::Disconnected;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to start websocket client");
        }
        return false;
    }
    
    // Wait for connection (with timeout)
    int timeout_ms = 10000;
    while (timeout_ms > 0 && !esp_websocket_client_is_connected(client_)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout_ms -= 100;
    }
    
    if (!esp_websocket_client_is_connected(client_)) {
        ESP_LOGE(TAG, "Websocket connection timeout");
        esp_websocket_client_stop(client_);
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
        state_ = ConnectionState::Disconnected;
        if (callbacks_.on_error) {
            callbacks_.on_error("Connection timeout");
        }
        return false;
    }
    
    state_ = ConnectionState::Connected;
    if (callbacks_.on_open) {
        callbacks_.on_open();
    }
    
    return true;
}

bool Esp32Transport::send(const std::string& message) {
    if (!client_ || state_ != ConnectionState::Connected) {
        return false;
    }
    
    int sent = esp_websocket_client_send_text(
        client_,
        message.c_str(),
        message.length(),
        pdMS_TO_TICKS(5000)  // 5 second timeout
    );
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send message");
        return false;
    }
    
    return true;
}

void Esp32Transport::close(const std::string& reason) {
    if (client_) {
        state_ = ConnectionState::Closing;
        esp_websocket_client_close(client_, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
    }
    state_ = ConnectionState::Disconnected;
}

ConnectionState Esp32Transport::state() const {
    return state_;
}

void Esp32Transport::set_callbacks(TransportCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

void Esp32Transport::poll(int timeout_ms) {
    // esp_websocket_client handles everything in its own task
    // Just yield to let other tasks run
    vTaskDelay(pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 1));
}

void Esp32Transport::run() {
    while (state_ == ConnectionState::Connected) {
        poll(100);
    }
}

} // namespace convex

#endif // CONVEX_PLATFORM_ESP32
