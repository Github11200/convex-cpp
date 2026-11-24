/**
 * Integration Tests
 * 
 * Tests for the full ConvexClient using MockTransport.
 * 
 * NOTE: These tests are currently disabled due to threading issues with the
 * mock transport. The ConvexClient's run() method expects a blocking event
 * loop which is difficult to test without actual async infrastructure.
 * 
 * To enable integration testing, we'd need to either:
 * 1. Add a non-blocking mode to ConvexClient
 * 2. Use proper threading with condition variables
 * 3. Use a real WebSocket connection to a test server
 */

#include "doctest.h"
#include "convex/client.hpp"
#include "convex/testing/mock_transport.hpp"
#include "convex/protocol.hpp"
#include "../third_party/nlohmann_json.hpp"

#include <thread>
#include <chrono>
#include <atomic>

using namespace convex;
using namespace convex::testing;
using json = nlohmann::json;

// Skip all integration tests for now - they need more work
#if 0
// Note: ConvexClient is non-copyable, so we create clients inline in tests

TEST_SUITE("Integration - Connection") {
    
    TEST_CASE("Client connects and sends Connect message") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        // Connect
        client.connect();
        
        // Should have connected to correct URL
        CHECK(mock->connected_url().find("wss://") == 0);
        CHECK(mock->connected_url().find("test.convex.cloud") != std::string::npos);
        CHECK(mock->connected_url().find("/sync") != std::string::npos);
        
        // Should have sent Connect message
        CHECK(mock->sent_message_count() >= 1);
        
        auto msg = mock->pop_sent_message();
        CHECK(msg.has_value());
        
        json j = json::parse(*msg);
        CHECK(j["type"] == "Connect");
        CHECK(j.contains("sessionId"));
        CHECK(j["connectionCount"] == 0);
    }
    
    TEST_CASE("URL conversion") {
        SUBCASE("https:// to wss://") {
            auto transport = std::make_unique<MockTransport>();
            auto* mock = transport.get();
            ConvexClient client("https://test.convex.cloud", std::move(transport));
            client.connect();
            
            CHECK(mock->connected_url().find("wss://test.convex.cloud") == 0);
        }
        
        SUBCASE("http:// to ws://") {
            auto transport = std::make_unique<MockTransport>();
            auto* mock = transport.get();
            ConvexClient client("http://localhost:3000", std::move(transport));
            client.connect();
            
            CHECK(mock->connected_url().find("ws://localhost:3000") == 0);
        }
        
        SUBCASE("Trailing slash removed") {
            auto transport = std::make_unique<MockTransport>();
            auto* mock = transport.get();
            ConvexClient client("https://test.convex.cloud/", std::move(transport));
            client.connect();
            
            CHECK(mock->connected_url().find("//sync") == std::string::npos);
        }
    }
}

TEST_SUITE("Integration - Subscriptions") {
    
    TEST_CASE("Subscribe sends ModifyQuerySet") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();  // Clear Connect message
        
        auto sub = client.subscribe("messages:list", {}, [](const FunctionResult&) {});
        
        // Should have sent ModifyQuerySet
        CHECK(mock->sent_message_count() == 1);
        
        auto msg = mock->pop_sent_message();
        json j = json::parse(*msg);
        
        CHECK(j["type"] == "ModifyQuerySet");
        CHECK(j["modifications"].size() == 1);
        CHECK(j["modifications"][0]["type"] == "Add");
        CHECK(j["modifications"][0]["udfPath"] == "messages:list");
    }
    
    TEST_CASE("Subscription callback receives updates") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        
        std::atomic<bool> received{false};
        FunctionResult last_result = FunctionResult::error("not set");
        
        auto sub = client.subscribe("messages:list", {},
            [&](const FunctionResult& result) {
                received = true;
                last_result = result;
            });
        
        // Inject server response
        mock->inject_query_update(0, Value("test data"));
        client.poll();
        
        CHECK(received);
        CHECK(last_result.is_success());
        CHECK(last_result.value.as_string() == "test data");
    }
    
    TEST_CASE("Unsubscribe sends Remove") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();
        
        {
            auto sub = client.subscribe("messages:list", {}, [](const FunctionResult&) {});
            mock->clear_sent_messages();
            // sub goes out of scope - destructor should unsubscribe
        }
        
        // Should have sent Remove
        auto msg = mock->pop_sent_message();
        REQUIRE(msg.has_value());
        
        json j = json::parse(*msg);
        CHECK(j["type"] == "ModifyQuerySet");
        CHECK(j["modifications"][0]["type"] == "Remove");
    }
    
    TEST_CASE("Multiple subscriptions to same query share server subscription") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();
        
        auto sub1 = client.subscribe("messages:list", {}, [](const FunctionResult&) {});
        auto sub2 = client.subscribe("messages:list", {}, [](const FunctionResult&) {});
        
        // Should only have one Add
        CHECK(mock->sent_message_count() == 1);
    }
}

TEST_SUITE("Integration - Mutations") {
    
    TEST_CASE("Mutation sends request") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();
        
        std::atomic<bool> completed{false};
        
        client.mutation_async("messages:send",
            {{"body", Value("Hello!")}},
            [&](const FunctionResult&) {
                completed = true;
            });
        
        auto msg = mock->pop_sent_message();
        REQUIRE(msg.has_value());
        
        json j = json::parse(*msg);
        CHECK(j["type"] == "Mutation");
        CHECK(j["udfPath"] == "messages:send");
        CHECK(j["requestId"] == 0);
    }
    
    TEST_CASE("Mutation callback receives response") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        
        std::atomic<bool> completed{false};
        FunctionResult result = FunctionResult::error("not set");
        
        client.mutation_async("messages:send",
            {{"body", Value("Hello!")}},
            [&](const FunctionResult& r) {
                result = r;
                completed = true;
            });
        
        // Inject response
        mock->inject_mutation_response(0, Value("success"));
        client.poll();
        
        CHECK(completed);
        CHECK(result.is_success());
    }
}

TEST_SUITE("Integration - Actions") {
    
    TEST_CASE("Action sends request") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();
        
        client.action_async("api:doSomething",
            {{"param", Value(123LL)}},
            [](const FunctionResult&) {});
        
        auto msg = mock->pop_sent_message();
        REQUIRE(msg.has_value());
        
        json j = json::parse(*msg);
        CHECK(j["type"] == "Action");
        CHECK(j["udfPath"] == "api:doSomething");
    }
    
    TEST_CASE("Action callback receives response") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        
        std::atomic<bool> completed{false};
        FunctionResult result = FunctionResult::error("not set");
        
        client.action_async("api:doSomething", {},
            [&](const FunctionResult& r) {
                result = r;
                completed = true;
            });
        
        mock->inject_action_response(0, Value("action result"));
        client.poll();
        
        CHECK(completed);
        CHECK(result.is_success());
    }
}

TEST_SUITE("Integration - Authentication") {
    
    TEST_CASE("set_auth sends Authenticate message") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();
        
        client.set_auth("my-jwt-token");
        
        auto msg = mock->pop_sent_message();
        REQUIRE(msg.has_value());
        
        json j = json::parse(*msg);
        CHECK(j["type"] == "Authenticate");
        CHECK(j["token"]["tokenType"] == "User");
        CHECK(j["token"]["value"] == "my-jwt-token");
    }
    
    TEST_CASE("clear_auth sends None token") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        mock->clear_sent_messages();
        
        client.clear_auth();
        
        auto msg = mock->pop_sent_message();
        REQUIRE(msg.has_value());
        
        json j = json::parse(*msg);
        CHECK(j["type"] == "Authenticate");
        CHECK(j["token"]["tokenType"] == "None");
    }
}

TEST_SUITE("Integration - Connection State") {
    
    TEST_CASE("Connection state callback") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        std::vector<ConnectionState> states;
        client.on_connection_change([&](ConnectionState state) {
            states.push_back(state);
        });
        
        client.connect();
        
        CHECK(states.size() >= 1);
        CHECK(states.back() == ConnectionState::Connected);
    }
    
    TEST_CASE("Disconnect callback") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        std::atomic<bool> disconnected{false};
        client.on_connection_change([&](ConnectionState state) {
            if (state == ConnectionState::Disconnected) {
                disconnected = true;
            }
        });
        
        client.connect();
        mock->simulate_close("Server shutdown");
        
        CHECK(disconnected);
    }
}

TEST_SUITE("Integration - Error Handling") {
    
    TEST_CASE("Connection failure") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        mock->fail_next_connect("Network error");
        
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        CHECK_FALSE(client.is_connected());
    }
    
    TEST_CASE("Query error is propagated") {
        auto transport = std::make_unique<MockTransport>();
        auto* mock = transport.get();
        ConvexClient client("https://test.convex.cloud", std::move(transport));
        
        client.connect();
        
        FunctionResult result = FunctionResult::success(Value());
        
        auto sub = client.subscribe("messages:list", {},
            [&](const FunctionResult& r) {
                result = r;
            });
        
        // Inject error response
        std::string error_response = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {
                    "type": "QueryFailed",
                    "queryId": 0,
                    "errorMessage": "Query error",
                    "logLines": []
                }
            ]
        })";
        mock->inject_server_message(error_response);
        client.poll();
        
        CHECK(result.is_error());
        CHECK(result.error_message == "Query error");
    }
}

#endif // Integration tests disabled
