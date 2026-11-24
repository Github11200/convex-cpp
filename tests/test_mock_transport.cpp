/**
 * Mock Transport Tests
 * 
 * Tests for the mock transport used in testing.
 */

#include "doctest.h"
#include "convex/testing/mock_transport.hpp"
#include "convex/protocol.hpp"

using namespace convex;
using namespace convex::testing;

TEST_SUITE("MockTransport") {
    
    TEST_CASE("Initial state is disconnected") {
        MockTransport transport;
        CHECK(transport.state() == ConnectionState::Disconnected);
    }
    
    TEST_CASE("Connect succeeds by default") {
        MockTransport transport;
        
        bool open_called = false;
        transport.set_callbacks({
            .on_open = [&]() { open_called = true; }
        });
        
        bool result = transport.connect("wss://example.com/sync", "test-client");
        
        CHECK(result == true);
        CHECK(transport.state() == ConnectionState::Connected);
        CHECK(transport.connected_url() == "wss://example.com/sync");
        CHECK(transport.connected_client_id() == "test-client");
        CHECK(open_called);
    }
    
    TEST_CASE("Connect can be configured to fail") {
        MockTransport transport;
        transport.fail_next_connect("Network error");
        
        bool error_called = false;
        std::string error_msg;
        transport.set_callbacks({
            .on_error = [&](const std::string& err) { 
                error_called = true;
                error_msg = err;
            }
        });
        
        bool result = transport.connect("wss://example.com/sync", "test-client");
        
        CHECK(result == false);
        CHECK(transport.state() == ConnectionState::Disconnected);
        CHECK(error_called);
        CHECK(error_msg == "Network error");
    }
    
    TEST_CASE("Send captures messages") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        transport.send(R"({"type": "Ping"})");
        transport.send(R"({"type": "Connect"})");
        
        CHECK(transport.sent_message_count() == 2);
        
        auto msg1 = transport.pop_sent_message();
        CHECK(msg1.has_value());
        CHECK(*msg1 == R"({"type": "Ping"})");
        
        auto msg2 = transport.pop_sent_message();
        CHECK(msg2.has_value());
        CHECK(*msg2 == R"({"type": "Connect"})");
        
        auto msg3 = transport.pop_sent_message();
        CHECK_FALSE(msg3.has_value());
    }
    
    TEST_CASE("Send fails when disconnected") {
        MockTransport transport;
        // Not connected
        
        bool result = transport.send(R"({"type": "Ping"})");
        
        CHECK(result == false);
        CHECK(transport.sent_message_count() == 0);
    }
    
    TEST_CASE("Inject server messages") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        std::vector<std::string> received;
        transport.set_callbacks({
            .on_open = []() {},
            .on_message = [&](const std::string& msg) {
                received.push_back(msg);
            }
        });
        
        // Re-set callbacks after connect
        transport.inject_server_message(R"({"type": "Ping"})");
        transport.inject_server_message(R"({"type": "Pong"})");
        
        transport.poll(0);
        
        CHECK(received.size() == 2);
        CHECK(received[0] == R"({"type": "Ping"})");
        CHECK(received[1] == R"({"type": "Pong"})");
    }
    
    TEST_CASE("Inject query update") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        std::string received_msg;
        transport.set_callbacks({
            .on_open = []() {},
            .on_message = [&](const std::string& msg) {
                received_msg = msg;
            }
        });
        
        transport.inject_query_update(0, Value("test result"));
        transport.poll(0);
        
        CHECK(received_msg.find("QueryUpdated") != std::string::npos);
        CHECK(received_msg.find("test result") != std::string::npos);
    }
    
    TEST_CASE("Inject mutation response") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        std::string received_msg;
        transport.set_callbacks({
            .on_open = []() {},
            .on_message = [&](const std::string& msg) {
                received_msg = msg;
            }
        });
        
        transport.inject_mutation_response(42, Value("mutation result"));
        transport.poll(0);
        
        CHECK(received_msg.find("MutationResponse") != std::string::npos);
        CHECK(received_msg.find("42") != std::string::npos);
    }
    
    TEST_CASE("Simulate close") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        bool close_called = false;
        std::string close_reason;
        transport.set_callbacks({
            .on_open = []() {},
            .on_close = [&](const std::string& reason) {
                close_called = true;
                close_reason = reason;
            }
        });
        
        transport.simulate_close("Server shutdown");
        
        CHECK(close_called);
        CHECK(close_reason == "Server shutdown");
        CHECK(transport.state() == ConnectionState::Disconnected);
    }
    
    TEST_CASE("Close method") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        bool close_called = false;
        transport.set_callbacks({
            .on_open = []() {},
            .on_close = [&](const std::string&) {
                close_called = true;
            }
        });
        
        transport.close("Client closing");
        
        CHECK(close_called);
        CHECK(transport.state() == ConnectionState::Disconnected);
    }
    
    TEST_CASE("on_message_sent callback") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        
        std::vector<std::string> captured;
        transport.on_message_sent([&](const std::string& msg) {
            captured.push_back(msg);
        });
        
        transport.send("message1");
        transport.send("message2");
        
        CHECK(captured.size() == 2);
        CHECK(captured[0] == "message1");
        CHECK(captured[1] == "message2");
    }
    
    TEST_CASE("Reset clears state") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        transport.send("message");
        transport.inject_server_message("server message");
        
        transport.reset();
        
        CHECK(transport.state() == ConnectionState::Disconnected);
        CHECK(transport.connected_url().empty());
        CHECK(transport.sent_message_count() == 0);
    }
    
    TEST_CASE("Peek does not remove message") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        transport.send("message");
        
        auto peek1 = transport.peek_sent_message();
        auto peek2 = transport.peek_sent_message();
        
        CHECK(peek1.has_value());
        CHECK(peek2.has_value());
        CHECK(*peek1 == *peek2);
        CHECK(transport.sent_message_count() == 1);
    }
    
    TEST_CASE("Clear sent messages") {
        MockTransport transport;
        transport.connect("wss://example.com/sync", "test-client");
        transport.send("msg1");
        transport.send("msg2");
        
        CHECK(transport.sent_message_count() == 2);
        
        transport.clear_sent_messages();
        
        CHECK(transport.sent_message_count() == 0);
    }
}
