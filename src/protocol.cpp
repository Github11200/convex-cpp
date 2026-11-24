#include "convex/protocol.hpp"
#include "../third_party/nlohmann_json.hpp"
#include <stdexcept>
#include <cmath>
#include <bit>

using json = nlohmann::json;

namespace convex {

// ============================================================================
// Helper functions for Convex-specific JSON encoding
// ============================================================================

namespace {

// Standard base64 encoding for bytes
std::string base64_encode_bytes(const uint8_t* data, size_t len) {
    static const char* chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        
        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    
    return result;
}

// Decode base64 to bytes
std::vector<uint8_t> base64_decode_bytes(const std::string& encoded) {
    static const int8_t index[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    
    std::vector<uint8_t> result;
    result.reserve(encoded.size() * 3 / 4);
    
    for (size_t i = 0; i < encoded.size(); i += 4) {
        uint32_t n = 0;
        int valid_chars = 0;
        
        for (int j = 0; j < 4 && i + j < encoded.size(); ++j) {
            char c = encoded[i + j];
            if (c == '=') break;
            int8_t idx = index[static_cast<uint8_t>(c)];
            if (idx < 0) throw std::runtime_error("Invalid base64 character");
            n = (n << 6) | static_cast<uint32_t>(idx);
            valid_chars++;
        }
        
        // Shift to fill remaining bits
        n <<= (4 - valid_chars) * 6;
        
        if (valid_chars >= 2) result.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
        if (valid_chars >= 3) result.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        if (valid_chars >= 4) result.push_back(static_cast<uint8_t>(n & 0xFF));
    }
    
    if (base64_encode_bytes(result.data(), result.size()) != encoded) {
        throw std::runtime_error("Invalid base64 encoding");
    }
    return result;
}

std::string encode_le64(uint64_t value) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return base64_encode_bytes(bytes, 8);
}

uint64_t decode_le64(const std::string& encoded) {
    auto bytes = base64_decode_bytes(encoded);
    if (bytes.size() != 8) throw std::runtime_error("Expected an eight-byte number");

    uint64_t result = 0;
    for (size_t i = 0; i < 8; ++i) {
        result |= uint64_t{bytes[i]} << (i * 8);
    }
    return result;
}

enum class JsonFormat { Sync, Export };

json value_to_json(const Value& v, JsonFormat format = JsonFormat::Sync) {
    switch (v.type()) {
        case Value::Type::Null:
            return nullptr;
        case Value::Type::Int64:
            if (format == JsonFormat::Export) return std::to_string(v.as_int64());
            // Convex encodes Int64 as {"$integer": "base64"}
            return json{{"$integer", encode_le64(std::bit_cast<uint64_t>(v.as_int64()))}};
        case Value::Type::Float64: {
            double d = v.as_float64();
            if (format == JsonFormat::Export) {
                if (std::isnan(d)) return "NaN";
                if (std::isinf(d)) return std::signbit(d) ? "-Infinity" : "Infinity";
                return d;
            }
            if (!std::isfinite(d) || (d == 0 && std::signbit(d))) {
                return json{{"$float", encode_le64(std::bit_cast<uint64_t>(d))}};
            }
            return d;
        }
        case Value::Type::Boolean:
            return v.as_boolean();
        case Value::Type::String:
            return v.as_string();
        case Value::Type::Bytes: {
            // Convex encodes Bytes as {"$bytes": "base64"}
            const auto& bytes = v.as_bytes();
            auto encoded = base64_encode_bytes(bytes.data(), bytes.size());
            if (format == JsonFormat::Export) return encoded;
            return json{{"$bytes", std::move(encoded)}};
        }
        case Value::Type::Array: {
            json arr = json::array();
            for (const auto& elem : v.as_array()) {
                arr.push_back(value_to_json(elem, format));
            }
            return arr;
        }
        case Value::Type::Object: {
            json obj = json::object();
            for (const auto& [key, val] : v.as_object()) {
                obj[key] = value_to_json(val, format);
            }
            return obj;
        }
    }
    return nullptr;
}

// Convert JSON to Value
Value json_to_value(const json& j) {
    if (j.is_null()) {
        return Value();
    } else if (j.is_boolean()) {
        return Value(j.get<bool>());
    } else if (j.is_number()) {
        return Value(j.get<double>());
    } else if (j.is_string()) {
        return Value(j.get<std::string>());
    } else if (j.is_array()) {
        Array arr;
        for (const auto& elem : j) {
            arr.push_back(json_to_value(elem));
        }
        return Value(std::move(arr));
    } else if (j.is_object()) {
        // Check for special Convex types
        if (j.size() == 1 && j.contains("$integer")) {
            return Value(std::bit_cast<int64_t>(decode_le64(j["$integer"].get<std::string>())));
        } else if (j.size() == 1 && j.contains("$float")) {
            auto d = std::bit_cast<double>(decode_le64(j["$float"].get<std::string>()));
            if (std::isfinite(d) && d != 0) throw std::runtime_error("Float64 should be encoded as a number");
            return Value(d);
        } else if (j.size() == 1 && j.contains("$bytes")) {
            // Decode base64 bytes
            std::string encoded = j["$bytes"].get<std::string>();
            auto bytes = base64_decode_bytes(encoded);
            return Value(Bytes(bytes.begin(), bytes.end()));
        } else if (j.size() == 1 && (j.contains("$set") || j.contains("$map"))) {
            throw std::runtime_error("Set and Map are not supported Convex values");
        } else {
            // Regular object
            Object obj;
            for (auto& [key, val] : j.items()) {
                obj[key] = json_to_value(val);
            }
            return Value(std::move(obj));
        }
    }
    return Value();
}

// Parse StateVersion from JSON
StateVersion parse_state_version(const json& j) {
    StateVersion sv;
    sv.query_set = j.value("querySet", 0u);
    sv.identity = j.value("identity", 0u);
    // Timestamp may be encoded as string or number
    if (j.contains("ts")) {
        if (j["ts"].is_string()) {
            sv.ts = decode_le64(j["ts"].get<std::string>());
        } else {
            sv.ts = j["ts"].get<uint64_t>();
        }
    }
    return sv;
}

void parse_request_outcome(const json& j, bool& success, std::optional<Value>& result,
                           std::optional<ErrorPayload>& error) {
    success = j.at("success").get<bool>();
    const auto& payload = j.at("result");
    if (success) {
        result = json_to_value(payload);
    } else {
        ErrorPayload ep;
        ep.message = payload.get<std::string>();
        if (j.contains("errorData")) {
            ep.data = json_to_value(j["errorData"]);
        }
        error = std::move(ep);
    }
}

// Parse LogLines from JSON
LogLines parse_log_lines(const json& j) {
    LogLines lines;
    if (j.is_array()) {
        for (const auto& line : j) {
            if (line.is_string()) {
                lines.push_back(line.get<std::string>());
            }
        }
    }
    return lines;
}

} // anonymous namespace

std::string Value::export_json() const {
    return value_to_json(*this, JsonFormat::Export).dump();
}

// ============================================================================
// Public API implementation
// ============================================================================

std::string serialize_value(const Value& value) {
    return value_to_json(value).dump();
}

Value parse_value(const std::string& json_str) {
    return json_to_value(json::parse(json_str));
}

std::string serialize_args(const Object& args) {
    json arr = json::array();
    arr.push_back(value_to_json(Value(args)));
    return arr.dump();
}

std::string serialize_client_message(const ClientMessage& msg) {
    json j;
    
    std::visit([&j](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, ConnectMessage>) {
            j["type"] = "Connect";
            j["sessionId"] = arg.session_id;
            j["connectionCount"] = arg.connection_count;
            j["lastCloseReason"] = arg.last_close_reason;
            if (arg.max_observed_timestamp) {
                j["maxObservedTimestamp"] = encode_le64(*arg.max_observed_timestamp);
            }
            if (arg.client_ts) {
                j["clientTs"] = *arg.client_ts;
            }
        }
        else if constexpr (std::is_same_v<T, ModifyQuerySetMessage>) {
            j["type"] = "ModifyQuerySet";
            j["baseVersion"] = arg.base_version;
            j["newVersion"] = arg.new_version;
            j["modifications"] = json::array();
            for (const auto& mod : arg.modifications) {
                json m;
                if (mod.type == QuerySetModification::Type::Add && mod.query) {
                    m["type"] = "Add";
                    m["queryId"] = mod.query->query_id;
                    m["udfPath"] = mod.query->udf_path;
                    m["args"] = json::parse(mod.query->args_json);
                    if (mod.query->journal) {
                        m["journal"] = *mod.query->journal;
                    }
                    if (mod.query->component_path) {
                        m["componentPath"] = *mod.query->component_path;
                    }
                } else {
                    m["type"] = "Remove";
                    m["queryId"] = mod.query_id;
                }
                j["modifications"].push_back(m);
            }
        }
        else if constexpr (std::is_same_v<T, MutationMessage>) {
            j["type"] = "Mutation";
            j["requestId"] = arg.request_id;
            j["udfPath"] = arg.udf_path;
            j["args"] = json::parse(arg.args_json);
            if (arg.component_path) {
                j["componentPath"] = *arg.component_path;
            }
        }
        else if constexpr (std::is_same_v<T, ActionMessage>) {
            j["type"] = "Action";
            j["requestId"] = arg.request_id;
            j["udfPath"] = arg.udf_path;
            j["args"] = json::parse(arg.args_json);
            if (arg.component_path) {
                j["componentPath"] = *arg.component_path;
            }
        }
        else if constexpr (std::is_same_v<T, AuthenticateMessage>) {
            j["type"] = "Authenticate";
            j["baseVersion"] = arg.base_version;
            switch (arg.token.type) {
                case AuthenticationToken::Type::None:
                    j["tokenType"] = "None";
                    break;
                case AuthenticationToken::Type::User:
                    j["tokenType"] = "User";
                    j["value"] = arg.token.value;
                    break;
                case AuthenticationToken::Type::Admin:
                    j["tokenType"] = "Admin";
                    j["value"] = arg.token.value;
                    if (arg.token.acting_as) j["acting_as"] = value_to_json(Value(*arg.token.acting_as));
                    break;
            }
        }
    }, msg);
    
    return j.dump();
}

ServerMessage parse_server_message(const std::string& json_str) {
    json j = json::parse(json_str);
    
    std::string type = j.value("type", "");
    
    if (type == "Transition") {
        TransitionMessage msg;
        msg.start_version = parse_state_version(j["startVersion"]);
        msg.end_version = parse_state_version(j["endVersion"]);
        
        if (j.contains("modifications") && j["modifications"].is_array()) {
            for (const auto& mod : j["modifications"]) {
                StateModification sm;
                std::string mod_type = mod.value("type", "");
                sm.query_id = mod.value("queryId", 0u);
                
                if (mod_type == "QueryUpdated") {
                    sm.type = StateModification::Type::QueryUpdated;
                    if (mod.contains("value")) {
                        sm.value = json_to_value(mod["value"]);
                    }
                    sm.log_lines = parse_log_lines(mod.value("logLines", json::array()));
                    if (mod.contains("journal") && !mod["journal"].is_null()) {
                        sm.journal = mod["journal"].get<std::string>();
                    }
                }
                else if (mod_type == "QueryFailed") {
                    sm.type = StateModification::Type::QueryFailed;
                    sm.error_message = mod.value("errorMessage", "Unknown error");
                    if (mod.contains("errorData")) {
                        sm.error_data = json_to_value(mod["errorData"]);
                    }
                    sm.log_lines = parse_log_lines(mod.value("logLines", json::array()));
                }
                else if (mod_type == "QueryRemoved") {
                    sm.type = StateModification::Type::QueryRemoved;
                }
                
                msg.modifications.push_back(std::move(sm));
            }
        }
        
        if (j.contains("clientClockSkew") && !j["clientClockSkew"].is_null()) {
            msg.client_clock_skew = j["clientClockSkew"].get<int64_t>();
        }
        if (j.contains("serverTs") && !j["serverTs"].is_null()) {
            if (j["serverTs"].is_string()) {
                msg.server_ts = decode_le64(j["serverTs"].get<std::string>());
            } else {
                msg.server_ts = j["serverTs"].get<uint64_t>();
            }
        }
        
        return msg;
    }
    else if (type == "MutationResponse") {
        MutationResponseMessage msg;
        msg.request_id = j.value("requestId", 0u);
        msg.log_lines = parse_log_lines(j.value("logLines", json::array()));
        
        parse_request_outcome(j, msg.success, msg.result, msg.error);
        
        if (j.contains("ts") && !j["ts"].is_null()) {
            if (j["ts"].is_string()) {
                msg.ts = decode_le64(j["ts"].get<std::string>());
            } else {
                msg.ts = j["ts"].get<uint64_t>();
            }
        }
        
        return msg;
    }
    else if (type == "ActionResponse") {
        ActionResponseMessage msg;
        msg.request_id = j.value("requestId", 0u);
        msg.log_lines = parse_log_lines(j.value("logLines", json::array()));
        
        parse_request_outcome(j, msg.success, msg.result, msg.error);
        
        return msg;
    }
    else if (type == "AuthError") {
        AuthErrorMessage msg;
        msg.error_message = j.value("errorMessage", "Auth error");
        if (j.contains("baseVersion") && !j["baseVersion"].is_null()) {
            msg.base_version = j["baseVersion"].get<IdentityVersion>();
        }
        if (j.contains("authUpdateAttempted") && !j["authUpdateAttempted"].is_null()) {
            msg.auth_update_attempted = j["authUpdateAttempted"].get<bool>();
        }
        return msg;
    }
    else if (type == "FatalError") {
        FatalErrorMessage msg;
        msg.error_message = j.value("errorMessage", "Fatal error");
        return msg;
    }
    else if (type == "Ping") {
        return PingMessage{};
    }
    
    throw std::runtime_error("Unknown server message type: " + type);
}

} // namespace convex
