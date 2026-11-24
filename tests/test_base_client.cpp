/**
 * BaseConvexClient Tests
 * 
 * Tests for the synchronous state machine that handles Convex protocol logic.
 */

#include "doctest.h"
#include "convex/base_client.hpp"
#include "convex/protocol.hpp"
#include "../third_party/nlohmann_json.hpp"

using namespace convex;
using json = nlohmann::json;

TEST_SUITE("BaseConvexClient - Connection") {
    
    TEST_CASE("Connect message generation") {
        BaseConvexClient client;
        
        auto msg = client.connect_message("session-123", 0, "InitialConnect");
        
        CHECK(std::holds_alternative<ConnectMessage>(msg));
        auto& cm = std::get<ConnectMessage>(msg);
        
        CHECK(cm.session_id == "session-123");
        CHECK(cm.connection_count == 0);
        CHECK(cm.last_close_reason == "InitialConnect");
        CHECK_FALSE(cm.max_observed_timestamp.has_value());
    }
    
    TEST_CASE("Connect message with reconnect") {
        BaseConvexClient client;
        
        // First connection
        auto msg1 = client.connect_message("session", 0, "Initial");
        
        // Simulate receiving a timestamp
        std::string server_msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 0, "identity": 0, "ts": 12345},
            "modifications": []
        })";
        client.handle_message(parse_server_message(server_msg));
        
        // Reconnect should include max timestamp
        auto msg2 = client.connect_message("session", 1, "Reconnect");
        auto& cm = std::get<ConnectMessage>(msg2);
        
        CHECK(cm.connection_count == 1);
        CHECK(cm.max_observed_timestamp.has_value());
        CHECK(*cm.max_observed_timestamp == 12345);
    }
}

TEST_SUITE("BaseConvexClient - Subscriptions") {
    
    TEST_CASE("Subscribe to new query") {
        BaseConvexClient client;
        bool callback_called = false;
        
        auto [sub_id, msg] = client.subscribe(
            "messages:list",
            {},
            [&](const SubscriberId&, const FunctionResult&) {
                callback_called = true;
            }
        );
        
        // Should generate a message
        CHECK(msg.has_value());
        CHECK(std::holds_alternative<ModifyQuerySetMessage>(*msg));
        
        auto& mqsm = std::get<ModifyQuerySetMessage>(*msg);
        CHECK(mqsm.base_version == 0);
        CHECK(mqsm.new_version == 1);
        CHECK(mqsm.modifications.size() == 1);
        CHECK(mqsm.modifications[0].type == QuerySetModification::Type::Add);
        
        // Subscriber ID should be valid
        CHECK(sub_id.query_id == 0);
        CHECK(sub_id.index == 0);
    }
    
    TEST_CASE("Subscribe to same query twice shares subscription") {
        BaseConvexClient client;
        
        auto [sub_id1, msg1] = client.subscribe("test:query", {}, 
            [](const SubscriberId&, const FunctionResult&) {});
        auto [sub_id2, msg2] = client.subscribe("test:query", {}, 
            [](const SubscriberId&, const FunctionResult&) {});
        
        // First should generate message
        CHECK(msg1.has_value());
        
        // Second should NOT generate message (reuses existing)
        CHECK_FALSE(msg2.has_value());
        
        // But should have different subscriber IDs
        CHECK(sub_id1.query_id == sub_id2.query_id);
        CHECK(sub_id1.index != sub_id2.index);
    }
    
    TEST_CASE("Subscribe with different args creates different queries") {
        BaseConvexClient client;
        
        auto [sub_id1, msg1] = client.subscribe("test:query", 
            {{"id", Value(1LL)}},
            [](const SubscriberId&, const FunctionResult&) {});
        auto [sub_id2, msg2] = client.subscribe("test:query", 
            {{"id", Value(2LL)}},
            [](const SubscriberId&, const FunctionResult&) {});
        
        // Both should generate messages
        CHECK(msg1.has_value());
        CHECK(msg2.has_value());
        
        // Different query IDs
        CHECK(sub_id1.query_id != sub_id2.query_id);
    }
    
    TEST_CASE("Unsubscribe removes subscription") {
        BaseConvexClient client;
        
        auto [sub_id, _] = client.subscribe("test:query", {},
            [](const SubscriberId&, const FunctionResult&) {});
        
        auto msg = client.unsubscribe(sub_id);
        
        // Should generate Remove message
        CHECK(msg.has_value());
        CHECK(std::holds_alternative<ModifyQuerySetMessage>(*msg));
        
        auto& mqsm = std::get<ModifyQuerySetMessage>(*msg);
        CHECK(mqsm.modifications.size() == 1);
        CHECK(mqsm.modifications[0].type == QuerySetModification::Type::Remove);
    }
    
    TEST_CASE("Unsubscribe with multiple subscribers") {
        BaseConvexClient client;
        
        auto [sub_id1, _1] = client.subscribe("test:query", {},
            [](const SubscriberId&, const FunctionResult&) {});
        auto [sub_id2, _2] = client.subscribe("test:query", {},
            [](const SubscriberId&, const FunctionResult&) {});
        
        // First unsubscribe should NOT generate message
        auto msg1 = client.unsubscribe(sub_id1);
        CHECK_FALSE(msg1.has_value());
        
        // Second unsubscribe SHOULD generate message
        auto msg2 = client.unsubscribe(sub_id2);
        CHECK(msg2.has_value());
    }
    
    TEST_CASE("Get result after subscription") {
        BaseConvexClient client;
        
        auto [sub_id, _] = client.subscribe("test:query", {},
            [](const SubscriberId&, const FunctionResult&) {});
        
        // Initially no result
        CHECK_FALSE(client.get_result(sub_id).has_value());
        
        // Simulate receiving result
        std::string server_msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {
                    "type": "QueryUpdated",
                    "queryId": 0,
                    "value": "test result",
                    "logLines": []
                }
            ]
        })";
        client.handle_message(parse_server_message(server_msg));
        
        // Now should have result
        auto result = client.get_result(sub_id);
        CHECK(result.has_value());
        CHECK(result->is_success());
        CHECK(result->value.as_string() == "test result");
    }
    
    TEST_CASE("Callback called on query update") {
        BaseConvexClient client;
        
        int callback_count = 0;
        FunctionResult last_result = FunctionResult::error("not set");
        
        auto [sub_id, _] = client.subscribe("test:query", {},
            [&](const SubscriberId& id, const FunctionResult& result) {
                callback_count++;
                last_result = result;
            });
        
        // Send update
        std::string server_msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {
                    "type": "QueryUpdated",
                    "queryId": 0,
                    "value": 42,
                    "logLines": []
                }
            ]
        })";
        client.handle_message(parse_server_message(server_msg));
        
        CHECK(callback_count == 1);
        CHECK(last_result.is_success());
    }
    
    TEST_CASE("Multiple subscribers notified") {
        BaseConvexClient client;
        
        int count1 = 0, count2 = 0;
        
        auto [sub_id1, _1] = client.subscribe("test:query", {},
            [&](const SubscriberId&, const FunctionResult&) { count1++; });
        auto [sub_id2, _2] = client.subscribe("test:query", {},
            [&](const SubscriberId&, const FunctionResult&) { count2++; });
        
        std::string server_msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {"type": "QueryUpdated", "queryId": 0, "value": null, "logLines": []}
            ]
        })";
        client.handle_message(parse_server_message(server_msg));
        
        CHECK(count1 == 1);
        CHECK(count2 == 1);
    }
    
    TEST_CASE("Query failure handled") {
        BaseConvexClient client;
        
        FunctionResult last_result = FunctionResult::success(Value());
        
        auto [sub_id, _] = client.subscribe("test:query", {},
            [&](const SubscriberId&, const FunctionResult& result) {
                last_result = result;
            });
        
        std::string server_msg = R"({
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
        client.handle_message(parse_server_message(server_msg));
        
        CHECK(last_result.is_error());
        CHECK(last_result.error_message == "Query error");
    }
}

TEST_SUITE("BaseConvexClient - Mutations") {
    
    TEST_CASE("Mutation message generation") {
        BaseConvexClient client;
        
        bool callback_called = false;
        auto msg = client.mutation(
            "messages:send",
            {{"body", Value("hello")}},
            [&](SessionRequestSeqNumber, const FunctionResult&) {
                callback_called = true;
            }
        );
        
        CHECK(std::holds_alternative<MutationMessage>(msg));
        auto& mm = std::get<MutationMessage>(msg);
        
        CHECK(mm.udf_path == "messages:send");
        CHECK(mm.request_id == 0);  // First request
    }
    
    TEST_CASE("Mutation request ID increments") {
        BaseConvexClient client;
        
        auto msg1 = client.mutation("test:mutation", {},
            [](SessionRequestSeqNumber, const FunctionResult&) {});
        auto msg2 = client.mutation("test:mutation", {},
            [](SessionRequestSeqNumber, const FunctionResult&) {});
        
        CHECK(std::get<MutationMessage>(msg1).request_id == 0);
        CHECK(std::get<MutationMessage>(msg2).request_id == 1);
    }
    
    TEST_CASE("Mutation callback on response") {
        BaseConvexClient client;
        
        FunctionResult result = FunctionResult::error("not set");
        SessionRequestSeqNumber received_id = 999;
        
        auto msg = client.mutation("test:mutation", {},
            [&](SessionRequestSeqNumber id, const FunctionResult& r) {
                received_id = id;
                result = r;
            });
        
        // Simulate response
        std::string server_msg = R"({
            "type": "MutationResponse",
            "requestId": 0,
            "success": true,
            "result": "mutation result",
            "logLines": []
        })";
        client.handle_message(parse_server_message(server_msg));
        CHECK(received_id == 999);
        client.handle_message(TransitionMessage{});

        CHECK(received_id == 0);
        CHECK(result.is_success());
        CHECK(result.value.as_string() == "mutation result");
    }
    
    TEST_CASE("Mutation error response") {
        BaseConvexClient client;
        
        FunctionResult result = FunctionResult::success(Value());
        
        auto msg = client.mutation("test:mutation", {},
            [&](SessionRequestSeqNumber, const FunctionResult& r) {
                result = r;
            });
        
        std::string server_msg = R"({
            "type": "MutationResponse",
            "requestId": 0,
            "success": false,
            "result": "Something went wrong",
            "logLines": []
        })";
        client.handle_message(parse_server_message(server_msg));
        
        CHECK(result.is_error());
    }
}

TEST_SUITE("BaseConvexClient - Actions") {
    
    TEST_CASE("Action message generation") {
        BaseConvexClient client;
        
        auto msg = client.action(
            "api:doSomething",
            {{"param", Value(123LL)}},
            [](SessionRequestSeqNumber, const FunctionResult&) {}
        );
        
        CHECK(std::holds_alternative<ActionMessage>(msg));
        auto& am = std::get<ActionMessage>(msg);
        
        CHECK(am.udf_path == "api:doSomething");
    }
    
    TEST_CASE("Action callback on response") {
        BaseConvexClient client;
        
        FunctionResult result = FunctionResult::error("not set");
        
        auto msg = client.action("test:action", {},
            [&](SessionRequestSeqNumber, const FunctionResult& r) {
                result = r;
            });
        
        std::string server_msg = R"({
            "type": "ActionResponse",
            "requestId": 0,
            "success": true,
            "result": {"success": true},
            "logLines": []
        })";
        client.handle_message(parse_server_message(server_msg));
        
        CHECK(result.is_success());
    }
}

TEST_SUITE("BaseConvexClient - Authentication") {
    
    TEST_CASE("Authenticate message generation") {
        BaseConvexClient client;
        
        auto msg = client.authenticate(AuthenticationToken::user("my-jwt-token"));
        
        CHECK(std::holds_alternative<AuthenticateMessage>(msg));
        auto& am = std::get<AuthenticateMessage>(msg);
        
        CHECK(am.token.type == AuthenticationToken::Type::User);
        CHECK(am.token.value == "my-jwt-token");
    }
    
    TEST_CASE("Clear auth") {
        BaseConvexClient client;
        
        auto msg = client.clear_auth();
        
        CHECK(std::holds_alternative<AuthenticateMessage>(msg));
        auto& am = std::get<AuthenticateMessage>(msg);
        
        CHECK(am.token.type == AuthenticationToken::Type::None);
    }
}

TEST_SUITE("BaseConvexClient - Reconnection") {
    TEST_CASE("Restart replaces the outgoing queue rather than appending stale versions") {
        BaseConvexClient client(BaseConvexClient::OutgoingMode::QueueMessages);
        auto first = client.subscribe("a", {}, {}).first;
        client.subscribe("b", {}, {});
        client.unsubscribe(first);
        client.reconnect_messages();
        auto message = client.pop_next_message();
        REQUIRE(message);
        const auto& replay = std::get<ModifyQuerySetMessage>(*message);
        CHECK(replay.base_version == 0);
        CHECK(replay.new_version == 1);
        REQUIRE(replay.modifications.size() == 1);
        CHECK(replay.modifications.front().query->udf_path == "b.js:default");
        CHECK_FALSE(client.pop_next_message());

        client.subscribe("c", {}, {});
        message = client.pop_next_message();
        REQUIRE(message);
        CHECK(std::get<ModifyQuerySetMessage>(*message).base_version == 1);
    }

    TEST_CASE("Canonical paths deduplicate and acknowledged mutations survive auth changes") {
        BaseConvexClient client;
        auto [sub, add] = client.subscribe("items", {}, {});
        auto [second, duplicate] = client.subscribe("items.js:default", {}, {});
        CHECK(sub.query_id == second.query_id);
        CHECK_FALSE(duplicate);
        client.action("test:action", {}, {});
        std::optional<FunctionResult> result;
        auto acknowledged = client.mutation("test:mutation", {}, [&](auto, const auto& r) { result = r; });
        client.handle_message(MutationResponseMessage{1, true, Value("ok"), {}, 42, {}});
        CHECK_FALSE(result);
        CHECK(client.max_observed_timestamp() == 42);
        client.authenticate(AuthenticationToken::user("new-user"));
        auto replay = client.reconnect_messages();
        REQUIRE(replay.size() == 3);
        CHECK(serialize_client_message(replay.back()) == serialize_client_message(acknowledged));
        client.handle_message(TransitionMessage{});
        REQUIRE(result);
        CHECK(result->value.as_string() == "ok");
    }

    
    TEST_CASE("Reconnect messages restore subscriptions") {
        BaseConvexClient client;
        
        // Create subscriptions
        client.subscribe("query1", {}, 
            [](const SubscriberId&, const FunctionResult&) {});
        client.subscribe("query2", {{"id", Value(1LL)}}, 
            [](const SubscriberId&, const FunctionResult&) {});
        
        // Get reconnect messages
        auto messages = client.reconnect_messages();
        
        // Should have one ModifyQuerySet with both queries
        CHECK(messages.size() >= 1);
        
        bool found_modify = false;
        for (const auto& msg : messages) {
            if (std::holds_alternative<ModifyQuerySetMessage>(msg)) {
                found_modify = true;
                auto& mqsm = std::get<ModifyQuerySetMessage>(msg);
                CHECK(mqsm.modifications.size() == 2);
            }
        }
        CHECK(found_modify);
    }
    
    TEST_CASE("Reconnect messages include auth") {
        BaseConvexClient client;
        
        // Authenticate
        client.authenticate(AuthenticationToken::user("token"));
        
        auto messages = client.reconnect_messages();
        
        bool found_auth = false;
        for (const auto& msg : messages) {
            if (std::holds_alternative<AuthenticateMessage>(msg)) {
                found_auth = true;
            }
        }
        CHECK(found_auth);
    }
    
    TEST_CASE("Reconnect replays pending actions and mutations in order") {
        BaseConvexClient client;
        auto mutation = client.mutation("test:mutation", {{"x", Value("y")}}, {});
        auto action = client.action("test:action", {}, {});
        auto replay = client.reconnect_messages();
        REQUIRE(replay.size() == 3);
        const auto& query_set = std::get<ModifyQuerySetMessage>(replay[0]);
        CHECK(query_set.base_version == 0);
        CHECK(query_set.new_version == 1);
        CHECK(serialize_client_message(replay[1]) == serialize_client_message(mutation));
        CHECK(serialize_client_message(replay[2]) == serialize_client_message(action));
    }

    TEST_CASE("Reconnect does not replay unfinished requests under changed auth") {
        BaseConvexClient client;
        std::optional<FunctionResult> failure;
        client.mutation("test:mutation", {}, [&](auto, const auto& result) {
            failure = result;
            CHECK_FALSE(client.max_observed_timestamp().has_value());
        });
        client.authenticate(AuthenticationToken::user("different-user"));
        auto current = client.action("test:action", {}, {});
        auto replay = client.reconnect_messages();
        REQUIRE(replay.size() == 3);
        CHECK(std::get<AuthenticateMessage>(replay[0]).base_version == 0);
        CHECK(serialize_client_message(replay[2]) == serialize_client_message(current));
        REQUIRE(failure.has_value());
        CHECK(failure->error_message == "Authentication changed before the request completed");
    }

    TEST_CASE("Auth fetcher signs out on initial failure and keeps cached token on refresh failure") {
        BaseConvexClient client;
        client.authenticate(AuthenticationToken::user("previous"));
        auto failed = client.set_auth_fetcher([](bool) -> AuthenticationToken {
            throw std::runtime_error("unavailable");
        });
        CHECK(std::get<AuthenticateMessage>(failed).token.type == AuthenticationToken::Type::None);
        int refreshes = 0;
        auto set = client.set_auth_fetcher([&](bool force) {
            if (force && ++refreshes == 2) throw std::runtime_error("unavailable");
            return AuthenticationToken::user(force ? "refreshed" : "initial");
        });
        CHECK(std::get<AuthenticateMessage>(set).token.value == "initial");
        auto first = client.reconnect_messages();
        auto second = client.reconnect_messages();
        CHECK(refreshes == 2);
        CHECK(std::get<AuthenticateMessage>(first[0]).token.value == "refreshed");
        CHECK(std::get<AuthenticateMessage>(second[0]).token.value == "refreshed");
    }
}

TEST_SUITE("BaseConvexClient - Message Handling") {
    
    TEST_CASE("Ping message does nothing") {
        BaseConvexClient client;
        
        // Should not throw
        std::string server_msg = R"({"type": "Ping"})";
        CHECK_NOTHROW(client.handle_message(parse_server_message(server_msg)));
    }
    
    TEST_CASE("Unknown query ID ignored") {
        BaseConvexClient client;
        
        // Subscribe to query 0
        client.subscribe("test:query", {},
            [](const SubscriberId&, const FunctionResult&) {});
        
        // Update for query 5 (doesn't exist) - should not throw
        std::string server_msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {"type": "QueryUpdated", "queryId": 5, "value": null, "logLines": []}
            ]
        })";
        CHECK_NOTHROW(client.handle_message(parse_server_message(server_msg)));
    }
    
    TEST_CASE("Unknown request ID is a protocol error") {
        BaseConvexClient client;
        
        std::string server_msg = R"({
            "type": "MutationResponse",
            "requestId": 999,
            "success": true,
            "result": null,
            "logLines": []
        })";
        CHECK_THROWS_WITH(client.handle_message(parse_server_message(server_msg)), "Invalid request id from server");
    }

    TEST_CASE("Invalid transition does not advance the remote state") {
        BaseConvexClient client;
        TransitionMessage transition;
        transition.start_version.ts = 1;
        transition.end_version.ts = 10;
        CHECK_THROWS_WITH(client.handle_message(transition), "StartVersionMismatch");
        transition.start_version = {};
        CHECK_NOTHROW(client.handle_message(transition));
        CHECK_THROWS_WITH(client.handle_message(transition), "StartVersionMismatch");
    }
}
