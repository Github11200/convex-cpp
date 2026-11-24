#pragma once

#include "../transport.hpp"
#include "../protocol.hpp"
#include <queue>
#include <mutex>
#include <functional>

namespace convex {
namespace testing {

/**
 * Mock transport for testing the Convex client without a real server.
 * 
 * Allows:
 * - Injecting server messages
 * - Capturing client messages
 * - Simulating connection states
 * - Testing error conditions
 */
class MockTransport : public ITransport {
public:
    MockTransport() = default;
    
    // ITransport implementation
    bool connect(const std::string& url, const std::string& client_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        url_ = url;
        client_id_ = client_id;
        
        if (should_fail_connect_) {
            should_fail_connect_ = false;
            state_ = ConnectionState::Disconnected;
            if (callbacks_.on_error) {
                callbacks_.on_error(connect_error_message_);
            }
            return false;
        }
        
        state_ = ConnectionState::Connected;
        if (callbacks_.on_open) {
            callbacks_.on_open();
        }
        return true;
    }
    
    bool send(const std::string& message) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::Connected) {
            return false;
        }
        
        sent_messages_.push(message);
        auto callback = on_message_sent_;
        lock.unlock();
        if (callback) callback(message);
        
        return true;
    }
    
    void close(const std::string& reason = "") override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::Connected) {
            state_ = ConnectionState::Disconnected;
            if (callbacks_.on_close) {
                callbacks_.on_close(reason);
            }
        }
    }
    
    ConnectionState state() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    
    void set_callbacks(TransportCallbacks callbacks) override {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_ = std::move(callbacks);
    }
    
    void poll(int timeout_ms = 0) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Deliver any pending messages
        while (!pending_server_messages_.empty()) {
            std::string msg = pending_server_messages_.front();
            pending_server_messages_.pop();
            
            if (callbacks_.on_message) {
                callbacks_.on_message(msg);
            }
        }
    }
    
    void run() override {
        while (state() == ConnectionState::Connected) {
            poll(100);
        }
    }
    
    // ========================================================================
    // Test helpers
    // ========================================================================
    
    /**
     * Get the URL that was connected to
     */
    std::string connected_url() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return url_;
    }
    
    /**
     * Get the client ID that was used
     */
    std::string connected_client_id() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return client_id_;
    }
    
    /**
     * Get the number of messages sent by the client
     */
    size_t sent_message_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sent_messages_.size();
    }
    
    /**
     * Get and remove the next sent message
     */
    std::optional<std::string> pop_sent_message() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sent_messages_.empty()) {
            return std::nullopt;
        }
        std::string msg = sent_messages_.front();
        sent_messages_.pop();
        return msg;
    }
    
    /**
     * Peek at the next sent message without removing it
     */
    std::optional<std::string> peek_sent_message() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sent_messages_.empty()) {
            return std::nullopt;
        }
        return sent_messages_.front();
    }
    
    /**
     * Clear all sent messages
     */
    void clear_sent_messages() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!sent_messages_.empty()) {
            sent_messages_.pop();
        }
    }
    
    /**
     * Inject a server message to be delivered on next poll
     */
    void inject_server_message(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_server_messages_.push(message);
    }
    
    /**
     * Inject a server message (from ServerMessage type)
     */
    void inject_server_message(const ServerMessage& message) {
        // We need to serialize the ServerMessage to JSON
        // For now, this is a simplified version
        // In production, we'd have a serialize_server_message function
    }
    
    /**
     * Inject a Transition message for a query update
     */
    void inject_query_update(QueryId query_id, const Value& value) {
        std::string msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {
                    "type": "QueryUpdated",
                    "queryId": )" + std::to_string(query_id) + R"(,
                    "value": )" + serialize_value(value) + R"(,
                    "logLines": []
                }
            ]
        })";
        inject_server_message(msg);
    }
    
    /**
     * Inject a MutationResponse
     */
    void inject_mutation_response(SessionRequestSeqNumber request_id, const Value& result) {
        std::string msg = R"({
            "type": "MutationResponse",
            "success": true,
            "requestId": )" + std::to_string(request_id) + R"(,
            "result": )" + serialize_value(result) + R"(,
            "logLines": []
        })";
        inject_server_message(msg);
    }
    
    /**
     * Inject an ActionResponse
     */
    void inject_action_response(SessionRequestSeqNumber request_id, const Value& result) {
        std::string msg = R"({
            "type": "ActionResponse",
            "success": true,
            "requestId": )" + std::to_string(request_id) + R"(,
            "result": )" + serialize_value(result) + R"(,
            "logLines": []
        })";
        inject_server_message(msg);
    }
    
    /**
     * Inject a Ping message
     */
    void inject_ping() {
        inject_server_message(R"({"type": "Ping"})");
    }
    
    /**
     * Inject an error
     */
    void inject_error(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (callbacks_.on_error) {
            callbacks_.on_error(message);
        }
    }
    
    /**
     * Simulate connection close
     */
    void simulate_close(const std::string& reason = "Simulated close") {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::Disconnected;
        if (callbacks_.on_close) {
            callbacks_.on_close(reason);
        }
    }
    
    /**
     * Configure the mock to fail on connect
     */
    void fail_next_connect(const std::string& error_message = "Connection failed") {
        std::lock_guard<std::mutex> lock(mutex_);
        should_fail_connect_ = true;
        connect_error_message_ = error_message;
    }
    
    /**
     * Set a callback for when messages are sent
     */
    void on_message_sent(std::function<void(const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        on_message_sent_ = std::move(callback);
    }
    
    /**
     * Reset the mock to initial state
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::Disconnected;
        url_.clear();
        client_id_.clear();
        while (!sent_messages_.empty()) sent_messages_.pop();
        while (!pending_server_messages_.empty()) pending_server_messages_.pop();
        should_fail_connect_ = false;
        connect_error_message_.clear();
        on_message_sent_ = nullptr;
    }
    
private:
    mutable std::mutex mutex_;
    
    ConnectionState state_{ConnectionState::Disconnected};
    TransportCallbacks callbacks_;
    
    std::string url_;
    std::string client_id_;
    
    std::queue<std::string> sent_messages_;
    std::queue<std::string> pending_server_messages_;
    
    bool should_fail_connect_ = false;
    std::string connect_error_message_;
    
    std::function<void(const std::string&)> on_message_sent_;
};

} // namespace testing
} // namespace convex
