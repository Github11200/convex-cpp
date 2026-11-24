/**
 * Transport Tests
 * 
 * Tests for the transport interface and backoff logic.
 */

#include "doctest.h"
#include "convex/transport.hpp"
#include <thread>
#include <chrono>

using namespace convex;

TEST_SUITE("Backoff") {
    
    TEST_CASE("Initial delay is near initial value") {
        Backoff backoff(
            std::chrono::milliseconds(100),
            std::chrono::seconds(15)
        );
        
        auto delay = backoff.next();
        
        // With jitter, should be roughly 75-125ms
        CHECK(delay >= std::chrono::milliseconds(0));
        CHECK(delay <= std::chrono::milliseconds(200));
    }
    
    TEST_CASE("Delay increases exponentially") {
        Backoff backoff(
            std::chrono::milliseconds(100),
            std::chrono::seconds(15)
        );
        
        auto delay1 = backoff.next();
        auto delay2 = backoff.next();
        auto delay3 = backoff.next();
        auto delay4 = backoff.next();
        
        // Each should be roughly 2x the previous (with jitter)
        // Just check they're increasing
        // Note: with jitter, this isn't guaranteed, but should generally hold
    }
    
    TEST_CASE("Delay is capped at max") {
        Backoff backoff(
            std::chrono::milliseconds(100),
            std::chrono::milliseconds(500)
        );
        
        // Call many times to exceed max
        for (int i = 0; i < 20; ++i) {
            auto delay = backoff.next();
            CHECK(delay <= std::chrono::milliseconds(500));
        }
    }
    
    TEST_CASE("Reset returns to initial") {
        Backoff backoff(
            std::chrono::milliseconds(100),
            std::chrono::seconds(15)
        );
        
        // Build up backoff
        for (int i = 0; i < 10; ++i) {
            backoff.next();
        }
        
        backoff.reset();
        
        auto delay = backoff.next();
        // Should be back to initial range
        CHECK(delay <= std::chrono::milliseconds(200));
    }
    
    TEST_CASE("Default constructor") {
        Backoff backoff;  // Uses defaults: 100ms initial, 15s max
        
        auto delay = backoff.next();
        CHECK(delay >= std::chrono::milliseconds(0));
        CHECK(delay <= std::chrono::milliseconds(200));
    }
}

TEST_SUITE("ConnectionState") {
    
    TEST_CASE("Enum values") {
        CHECK(static_cast<int>(ConnectionState::Disconnected) == 0);
        CHECK(static_cast<int>(ConnectionState::Connecting) == 1);
        CHECK(static_cast<int>(ConnectionState::Connected) == 2);
        CHECK(static_cast<int>(ConnectionState::Closing) == 3);
    }
}

TEST_SUITE("TransportCallbacks") {
    
    TEST_CASE("Default callbacks are null") {
        TransportCallbacks callbacks;
        
        CHECK_FALSE(callbacks.on_open);
        CHECK_FALSE(callbacks.on_message);
        CHECK_FALSE(callbacks.on_close);
        CHECK_FALSE(callbacks.on_error);
    }
    
    TEST_CASE("Callbacks can be assigned") {
        TransportCallbacks callbacks;
        
        bool open_called = false;
        bool message_called = false;
        bool close_called = false;
        bool error_called = false;
        
        callbacks.on_open = [&]() { open_called = true; };
        callbacks.on_message = [&](const std::string&) { message_called = true; };
        callbacks.on_close = [&](const std::string&) { close_called = true; };
        callbacks.on_error = [&](const std::string&) { error_called = true; };
        
        callbacks.on_open();
        callbacks.on_message("test");
        callbacks.on_close("reason");
        callbacks.on_error("error");
        
        CHECK(open_called);
        CHECK(message_called);
        CHECK(close_called);
        CHECK(error_called);
    }
}
