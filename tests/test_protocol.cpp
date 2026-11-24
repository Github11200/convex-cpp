/**
 * Protocol Tests
 * 
 * Tests for protocol message serialization and deserialization.
 */

#include "doctest.h"
#include "convex/protocol.hpp"
#include "../third_party/nlohmann_json.hpp"
#include <cmath>

using namespace convex;
using json = nlohmann::json;

TEST_SUITE("Protocol - Value Serialization") {
    
    TEST_CASE("Serialize null") {
        Value v;
        std::string s = serialize_value(v);
        CHECK(s == "null");
    }
    
    TEST_CASE("Serialize boolean") {
        CHECK(serialize_value(Value(true)) == "true");
        CHECK(serialize_value(Value(false)) == "false");
    }
    
    TEST_CASE("Serialize float64") {
        SUBCASE("Regular float") {
            Value v(3.14);
            std::string s = serialize_value(v);
            json j = json::parse(s);
            CHECK(j.is_number_float());
            CHECK(j.get<double>() == doctest::Approx(3.14));
        }
        
        SUBCASE("NaN") {
            Value v(std::nan(""));
            std::string s = serialize_value(v);
            json j = json::parse(s);
            CHECK(j.is_object());
            CHECK(j.contains("$float"));
            CHECK(j["$float"] == "NaN");
        }
        
        SUBCASE("Infinity") {
            Value v(std::numeric_limits<double>::infinity());
            std::string s = serialize_value(v);
            json j = json::parse(s);
            CHECK(j["$float"] == "Infinity");
        }
        
        SUBCASE("Negative infinity") {
            Value v(-std::numeric_limits<double>::infinity());
            std::string s = serialize_value(v);
            json j = json::parse(s);
            CHECK(j["$float"] == "-Infinity");
        }
    }
    
    TEST_CASE("Serialize int64") {
        SUBCASE("Regular int") {
            Value v(42LL);
            std::string s = serialize_value(v);
            json j = json::parse(s);
            CHECK(j.is_object());
            CHECK(j.contains("$integer"));
            // Base64 encoded little-endian int64
        }
        
        SUBCASE("Large int roundtrip") {
            int64_t large = 9007199254740993LL;  // > JS safe integer
            Value v(large);
            std::string s = serialize_value(v);
            Value parsed = parse_value(s);
            CHECK(parsed.is_int64());
            CHECK(parsed.as_int64() == large);
        }
        
        SUBCASE("Negative int roundtrip") {
            int64_t neg = -123456789012345LL;
            Value v(neg);
            std::string s = serialize_value(v);
            Value parsed = parse_value(s);
            CHECK(parsed.as_int64() == neg);
        }
    }
    
    TEST_CASE("Serialize string") {
        Value v("hello world");
        std::string s = serialize_value(v);
        CHECK(s == "\"hello world\"");
    }
    
    TEST_CASE("Serialize bytes") {
        Bytes data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
        Value v(data);
        std::string s = serialize_value(v);
        json j = json::parse(s);
        CHECK(j.is_object());
        CHECK(j.contains("$bytes"));
    }
    
    TEST_CASE("Serialize array") {
        Array arr = {Value(1LL), Value("two"), Value(true)};
        Value v(arr);
        std::string s = serialize_value(v);
        json j = json::parse(s);
        CHECK(j.is_array());
        CHECK(j.size() == 3);
    }
    
    TEST_CASE("Serialize object") {
        Object obj = {{"key", Value("value")}, {"num", Value(42LL)}};
        Value v(obj);
        std::string s = serialize_value(v);
        json j = json::parse(s);
        CHECK(j.is_object());
        CHECK(j.contains("key"));
        CHECK(j.contains("num"));
    }
    
    TEST_CASE("Parse value") {
        SUBCASE("Parse null") {
            Value v = parse_value("null");
            CHECK(v.is_null());
        }
        
        SUBCASE("Parse boolean") {
            CHECK(parse_value("true").as_boolean() == true);
            CHECK(parse_value("false").as_boolean() == false);
        }
        
        SUBCASE("Parse number as float") {
            Value v = parse_value("3.14");
            CHECK(v.is_float64());
            CHECK(v.as_float64() == doctest::Approx(3.14));
        }
        
        SUBCASE("Parse integer as int") {
            Value v = parse_value("42");
            // JSON integers are parsed as int64
            CHECK(v.is_int64());
            CHECK(v.as_int64() == 42);
        }
        
        SUBCASE("Parse string") {
            Value v = parse_value("\"hello\"");
            CHECK(v.is_string());
            CHECK(v.as_string() == "hello");
        }
        
        SUBCASE("Parse array") {
            Value v = parse_value("[1, 2, 3]");
            CHECK(v.is_array());
            CHECK(v.as_array().size() == 3);
        }
        
        SUBCASE("Parse object") {
            Value v = parse_value("{\"a\": 1, \"b\": 2}");
            CHECK(v.is_object());
            CHECK(v.as_object().size() == 2);
        }
        
        SUBCASE("Parse $integer") {
            // 42 as base64 little-endian
            Value v = parse_value("{\"$integer\": \"KgAAAAAAAAA=\"}");
            CHECK(v.is_int64());
            CHECK(v.as_int64() == 42);
        }
        
        SUBCASE("Parse $float NaN") {
            Value v = parse_value("{\"$float\": \"NaN\"}");
            CHECK(v.is_float64());
            CHECK(std::isnan(v.as_float64()));
        }
        
        SUBCASE("Parse $float Infinity") {
            Value v = parse_value("{\"$float\": \"Infinity\"}");
            CHECK(v.is_float64());
            CHECK(std::isinf(v.as_float64()));
            CHECK(v.as_float64() > 0);
        }
    }
    
    TEST_CASE("Roundtrip value") {
        SUBCASE("Complex nested structure") {
            Object inner = {{"x", Value(100LL)}, {"y", Value(200LL)}};
            Array arr = {Value(1LL), Value(2LL), Value(inner)};
            Object outer = {
                {"name", Value("test")},
                {"data", Value(arr)},
                {"active", Value(true)}
            };
            Value original(outer);
            
            std::string serialized = serialize_value(original);
            Value parsed = parse_value(serialized);
            
            CHECK(parsed.is_object());
            CHECK(parsed["name"].as_string() == "test");
            CHECK(parsed["active"].as_boolean() == true);
            CHECK(parsed["data"].is_array());
            CHECK(parsed["data"][2]["x"].as_int64() == 100);
        }
    }
}

TEST_SUITE("Protocol - Client Messages") {
    
    TEST_CASE("Serialize Connect message") {
        ConnectMessage msg;
        msg.session_id = "test-session-123";
        msg.connection_count = 5;
        msg.last_close_reason = "TestReason";
        msg.max_observed_timestamp = std::nullopt;
        
        ClientMessage cm = msg;
        std::string s = serialize_client_message(cm);
        json j = json::parse(s);
        
        CHECK(j["type"] == "Connect");
        CHECK(j["sessionId"] == "test-session-123");
        CHECK(j["connectionCount"] == 5);
        CHECK(j["lastCloseReason"] == "TestReason");
        CHECK(j["maxObservedTimestamp"].is_null());
    }
    
    TEST_CASE("Serialize Connect with timestamp") {
        ConnectMessage msg;
        msg.session_id = "session";
        msg.connection_count = 1;
        msg.last_close_reason = "Reconnect";
        msg.max_observed_timestamp = 1234567890ULL;
        
        ClientMessage cm = msg;
        std::string s = serialize_client_message(cm);
        json j = json::parse(s);
        
        CHECK(j["type"] == "Connect");
        CHECK(j.contains("maxObservedTimestamp"));
        CHECK(j["maxObservedTimestamp"].is_string());  // Base64 encoded
    }
    
    TEST_CASE("Serialize ModifyQuerySet - Add") {
        ModifyQuerySetMessage msg;
        msg.base_version = 0;
        msg.new_version = 1;
        
        QuerySetModification mod;
        mod.type = QuerySetModification::Type::Add;
        mod.query_id = 0;
        
        Query q;
        q.query_id = 0;
        q.udf_path = "messages:list";
        q.args_json = "[{}]";
        mod.query = q;
        
        msg.modifications.push_back(mod);
        
        ClientMessage cm = msg;
        std::string s = serialize_client_message(cm);
        json j = json::parse(s);
        
        CHECK(j["type"] == "ModifyQuerySet");
        CHECK(j["baseVersion"] == 0);
        CHECK(j["newVersion"] == 1);
        CHECK(j["modifications"].is_array());
        CHECK(j["modifications"].size() == 1);
        CHECK(j["modifications"][0]["type"] == "Add");
        CHECK(j["modifications"][0]["queryId"] == 0);
        CHECK(j["modifications"][0]["udfPath"] == "messages:list");
    }
    
    TEST_CASE("Serialize ModifyQuerySet - Remove") {
        ModifyQuerySetMessage msg;
        msg.base_version = 5;
        msg.new_version = 6;
        
        QuerySetModification mod;
        mod.type = QuerySetModification::Type::Remove;
        mod.query_id = 3;
        
        msg.modifications.push_back(mod);
        
        ClientMessage cm = msg;
        std::string s = serialize_client_message(cm);
        json j = json::parse(s);
        
        CHECK(j["type"] == "ModifyQuerySet");
        CHECK(j["modifications"][0]["type"] == "Remove");
        CHECK(j["modifications"][0]["queryId"] == 3);
    }
    
    TEST_CASE("Serialize Mutation") {
        MutationMessage msg;
        msg.request_id = 42;
        msg.udf_path = "messages:send";
        msg.args_json = "[{\"body\": \"hello\"}]";
        
        ClientMessage cm = msg;
        std::string s = serialize_client_message(cm);
        json j = json::parse(s);
        
        CHECK(j["type"] == "Mutation");
        CHECK(j["requestId"] == 42);
        CHECK(j["udfPath"] == "messages:send");
        CHECK(j["args"].is_array());
    }
    
    TEST_CASE("Serialize Action") {
        ActionMessage msg;
        msg.request_id = 100;
        msg.udf_path = "api:doSomething";
        msg.args_json = "[{\"param\": 123}]";
        
        ClientMessage cm = msg;
        std::string s = serialize_client_message(cm);
        json j = json::parse(s);
        
        CHECK(j["type"] == "Action");
        CHECK(j["requestId"] == 100);
        CHECK(j["udfPath"] == "api:doSomething");
    }
    
    TEST_CASE("Serialize Authenticate") {
        SUBCASE("User token") {
            AuthenticateMessage msg;
            msg.base_version = 0;
            msg.token = AuthenticationToken::user("jwt-token-here");
            
            ClientMessage cm = msg;
            std::string s = serialize_client_message(cm);
            json j = json::parse(s);
            
            CHECK(j["type"] == "Authenticate");
            CHECK(j["baseVersion"] == 0);
            CHECK(j["token"]["tokenType"] == "User");
            CHECK(j["token"]["value"] == "jwt-token-here");
        }
        
        SUBCASE("No token") {
            AuthenticateMessage msg;
            msg.base_version = 1;
            msg.token = AuthenticationToken::none();
            
            ClientMessage cm = msg;
            std::string s = serialize_client_message(cm);
            json j = json::parse(s);
            
            CHECK(j["token"]["tokenType"] == "None");
        }
    }
    
    TEST_CASE("serialize_args helper") {
        Object args = {{"body", Value("test")}, {"count", Value(5LL)}};
        std::string s = serialize_args(args);
        json j = json::parse(s);
        
        CHECK(j.is_array());
        CHECK(j.size() == 1);
        CHECK(j[0].is_object());
        CHECK(j[0]["body"] == "test");
    }
}

TEST_SUITE("Protocol - Server Messages") {
    
    TEST_CASE("Parse Ping") {
        std::string msg = R"({"type": "Ping"})";
        ServerMessage sm = parse_server_message(msg);
        CHECK(std::holds_alternative<PingMessage>(sm));
    }
    
    TEST_CASE("Parse FatalError") {
        std::string msg = R"({"type": "FatalError", "errorMessage": "Something went wrong"})";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<FatalErrorMessage>(sm));
        auto& fem = std::get<FatalErrorMessage>(sm);
        CHECK(fem.error_message == "Something went wrong");
    }
    
    TEST_CASE("Parse AuthError") {
        std::string msg = R"({
            "type": "AuthError",
            "errorMessage": "Token expired",
            "baseVersion": 3,
            "authUpdateAttempted": true
        })";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<AuthErrorMessage>(sm));
        auto& aem = std::get<AuthErrorMessage>(sm);
        CHECK(aem.error_message == "Token expired");
        CHECK(aem.base_version.has_value());
        CHECK(*aem.base_version == 3);
        CHECK(aem.auth_update_attempted.has_value());
        CHECK(*aem.auth_update_attempted == true);
    }
    
    TEST_CASE("Parse Transition - QueryUpdated") {
        std::string msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 0, "identity": 0, "ts": 0},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "modifications": [
                {
                    "type": "QueryUpdated",
                    "queryId": 0,
                    "value": [{"_id": "abc", "body": "hello"}],
                    "logLines": ["Log line 1"],
                    "journal": null
                }
            ]
        })";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<TransitionMessage>(sm));
        auto& tm = std::get<TransitionMessage>(sm);
        
        CHECK(tm.start_version.query_set == 0);
        CHECK(tm.end_version.query_set == 1);
        CHECK(tm.modifications.size() == 1);
        
        auto& mod = tm.modifications[0];
        CHECK(mod.type == StateModification::Type::QueryUpdated);
        CHECK(mod.query_id == 0);
        CHECK(mod.value.has_value());
        CHECK(mod.value->is_array());
        CHECK(mod.log_lines.size() == 1);
    }
    
    TEST_CASE("Parse Transition - QueryFailed") {
        std::string msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 1, "identity": 0, "ts": 100},
            "endVersion": {"querySet": 1, "identity": 0, "ts": 200},
            "modifications": [
                {
                    "type": "QueryFailed",
                    "queryId": 0,
                    "errorMessage": "Query error",
                    "logLines": [],
                    "journal": null
                }
            ]
        })";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<TransitionMessage>(sm));
        auto& tm = std::get<TransitionMessage>(sm);
        
        CHECK(tm.modifications.size() == 1);
        auto& mod = tm.modifications[0];
        CHECK(mod.type == StateModification::Type::QueryFailed);
        CHECK(mod.error_message.has_value());
        CHECK(*mod.error_message == "Query error");
    }
    
    TEST_CASE("Parse Transition - QueryRemoved") {
        std::string msg = R"({
            "type": "Transition",
            "startVersion": {"querySet": 2, "identity": 0, "ts": 200},
            "endVersion": {"querySet": 3, "identity": 0, "ts": 300},
            "modifications": [
                {
                    "type": "QueryRemoved",
                    "queryId": 1
                }
            ]
        })";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<TransitionMessage>(sm));
        auto& tm = std::get<TransitionMessage>(sm);
        auto& mod = tm.modifications[0];
        CHECK(mod.type == StateModification::Type::QueryRemoved);
        CHECK(mod.query_id == 1);
    }
    
    TEST_CASE("Parse MutationResponse - Success") {
        std::string msg = R"({
            "type": "MutationResponse",
            "requestId": 1,
            "result": {"_id": "new-id"},
            "logLines": []
        })";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<MutationResponseMessage>(sm));
        auto& mrm = std::get<MutationResponseMessage>(sm);
        CHECK(mrm.request_id == 1);
        CHECK(mrm.success == true);
        CHECK(mrm.result.has_value());
        CHECK(mrm.result->is_object());
    }
    
    TEST_CASE("Parse ActionResponse - Success") {
        std::string msg = R"({
            "type": "ActionResponse",
            "requestId": 5,
            "result": "action result",
            "logLines": ["Action log"]
        })";
        ServerMessage sm = parse_server_message(msg);
        
        CHECK(std::holds_alternative<ActionResponseMessage>(sm));
        auto& arm = std::get<ActionResponseMessage>(sm);
        CHECK(arm.request_id == 5);
        CHECK(arm.success == true);
        CHECK(arm.result.has_value());
        CHECK(arm.result->as_string() == "action result");
        CHECK(arm.log_lines.size() == 1);
    }
    
    TEST_CASE("Parse unknown message type throws") {
        std::string msg = R"({"type": "UnknownType"})";
        CHECK_THROWS(parse_server_message(msg));
    }
    
    TEST_CASE("Parse invalid JSON throws") {
        std::string msg = "not valid json";
        CHECK_THROWS(parse_server_message(msg));
    }
}
