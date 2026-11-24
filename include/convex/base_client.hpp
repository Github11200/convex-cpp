#pragma once

#include "value.hpp"
#include "protocol.hpp"
#include "error.hpp"
#include <map>
#include <set>
#include <functional>
#include <optional>
#include <queue>
#include <mutex>
#include <memory>

namespace convex {

/**
 * Identifier for a subscription to a query
 */
struct SubscriberId {
    QueryId query_id;
    uint32_t index;
    
    bool operator<(const SubscriberId& other) const {
        if (query_id != other.query_id) return query_id < other.query_id;
        return index < other.index;
    }
    
    bool operator==(const SubscriberId& other) const {
        return query_id == other.query_id && index == other.index;
    }
};

/**
 * Result of a query or mutation
 */
struct FunctionResult {
    enum class Type { Value, Error, ConvexError };
    
    Type type = Type::Value;
    Value value;
    std::string error_message;
    Value error_data;  // For ConvexError
    
    static FunctionResult success(Value v) {
        return {Type::Value, std::move(v), "", Value()};
    }
    
    static FunctionResult error(const std::string& msg) {
        return {Type::Error, Value(), msg, Value()};
    }
    
    static FunctionResult convex_error(const std::string& msg, Value data) {
        return {Type::ConvexError, Value(), msg, std::move(data)};
    }
    
    bool is_success() const { return type == Type::Value; }
    bool is_error() const { return type != Type::Value; }
    bool operator==(const FunctionResult&) const = default;
};

class QueryResults {
public:
    using Entries = std::map<SubscriberId, std::optional<FunctionResult>>;
    std::optional<FunctionResult> get(const SubscriberId& id) const {
        auto it = entries_.find(id);
        return it == entries_.end() ? std::nullopt : it->second;
    }
    size_t size() const { return entries_.size(); }
    size_t len() const { return size(); }
    bool empty() const { return entries_.empty(); }
    auto begin() const { return entries_.begin(); }
    auto end() const { return entries_.end(); }

private:
    friend class BaseConvexClient;
    Entries entries_;
};

/**
 * Callback types
 */
using QueryCallback = std::function<void(const SubscriberId&, const FunctionResult&)>;
using MutationCallback = std::function<void(SessionRequestSeqNumber, const FunctionResult&)>;
using ActionCallback = std::function<void(SessionRequestSeqNumber, const FunctionResult&)>;
using AuthCallback = std::function<AuthenticationToken(bool force_refresh)>;
using AuthTokenFetcher = AuthCallback;

/**
 * Internal query state
 */
struct LocalQuery {
    QueryId id;
    std::string udf_path;
    Object args;
    uint32_t num_subscribers;
    uint32_t subscription_index;
};

/**
 * Pending request
 */
namespace detail {
class RequestManager;
class ClientWorker;
}

/**
 * BaseConvexClient - The synchronous state machine for Convex sync protocol.
 * 
 * This class is platform-agnostic and handles:
 * - Query subscription management
 * - Mutation/action request tracking
 * - Protocol state versioning
 * - Message generation and response handling
 * 
 * It does NOT handle:
 * - WebSocket connections (use ITransport)
 * - Threading (handled by ConvexClient)
 * - Reconnection logic (handled by ConvexClient)
 */
class BaseConvexClient {
public:
    enum class OutgoingMode { ReturnMessages, QueueMessages };
    explicit BaseConvexClient(OutgoingMode mode = OutgoingMode::ReturnMessages);
    ~BaseConvexClient();
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * Generate the initial Connect message for a new WebSocket connection
     */
    ClientMessage connect_message(
        const SessionId& session_id,
        uint32_t connection_count,
        const std::string& close_reason
    );
    
    /**
     * Generate messages to restore state after reconnection
     * Returns messages to re-subscribe to all active queries and re-authenticate
     */
    std::vector<ClientMessage> reconnect_messages();
    // In QueueMessages mode, drain generated messages in version order.
    std::optional<ClientMessage> pop_next_message();
    
    /**
     * Get the max observed timestamp (for reconnection)
     */
    std::optional<Timestamp> max_observed_timestamp() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_observed_ts_;
    }
    
    // ========================================================================
    // Query Subscriptions
    // ========================================================================
    
    /**
     * Subscribe to a query
     * @param udf_path The path to the query function (e.g., "messages:list")
     * @param args Query arguments
     * @param callback Called when query result updates
     * @return Subscriber ID and optional message to send
     */
    std::pair<SubscriberId, std::optional<ClientMessage>> subscribe(
        const std::string& udf_path,
        const Object& args,
        QueryCallback callback
    );
    
    /**
     * Unsubscribe from a query
     * @param subscriber_id The subscriber to remove
     * @return Optional message to send (if this was the last subscriber)
     */
    std::optional<ClientMessage> unsubscribe(const SubscriberId& subscriber_id);
    
    /**
     * Get current result for a subscription
     */
    std::optional<FunctionResult> get_result(const SubscriberId& subscriber_id) const;
    QueryResults latest_results() const;
    
    // ========================================================================
    // Mutations and Actions
    // ========================================================================
    
    /**
     * Create a mutation request
     * @param udf_path The path to the mutation function
     * @param args Mutation arguments
     * @param callback Called when mutation completes
     * @return Message to send
     */
    ClientMessage mutation(
        const std::string& udf_path,
        const Object& args,
        MutationCallback callback
    );
    
    /**
     * Create an action request
     * @param udf_path The path to the action function
     * @param args Action arguments
     * @param callback Called when action completes
     * @return Message to send
     */
    ClientMessage action(
        const std::string& udf_path,
        const Object& args,
        ActionCallback callback
    );
    
    // ========================================================================
    // Authentication
    // ========================================================================
    
    /**
     * Set the authentication token fetcher
     */
    ClientMessage set_auth_fetcher(AuthCallback fetcher);
    
    /**
     * Create an authentication message
     */
    ClientMessage authenticate(const AuthenticationToken& token);
    
    /**
     * Clear authentication
     */
    ClientMessage clear_auth();
    
    // ========================================================================
    // Message Handling
    // ========================================================================
    
    /**
     * Handle a message received from the server
     * May trigger callbacks for query updates, mutation responses, etc.
     */
    std::optional<QueryResults> handle_message(const ServerMessage& message);
    
private:
    friend class detail::ClientWorker;
    void enqueue_locked(const ClientMessage& message);
    QueryResults snapshot_locked() const;
    // Query token for deduplication
    std::string make_query_token(const std::string& udf_path, const Object& args) const;
    
    // State
    QueryId next_query_id_ = 0;
    QuerySetVersion query_set_version_ = 0;
    IdentityVersion identity_version_ = 0;
    SessionRequestSeqNumber next_request_id_ = 0;
    std::optional<Timestamp> max_observed_ts_;
    
    // Query subscriptions
    std::map<std::string, LocalQuery> query_set_;  // token -> query
    std::map<QueryId, std::string> query_id_to_token_;
    std::map<QueryId, FunctionResult> latest_results_;
    std::set<SubscriberId> subscribers_;
    std::map<SubscriberId, QueryCallback> subscriber_callbacks_;
    
    // Pending requests
    std::unique_ptr<detail::RequestManager> requests_;
    SessionRequestSeqNumber auth_changed_at_ = 0;
    StateVersion remote_version_{};
    std::map<QueryId, FunctionResult> remote_results_;
    bool queue_messages_;
    std::queue<ClientMessage> outgoing_messages_;
    
    // Authentication
    AuthenticationToken current_token_;
    AuthCallback auth_fetcher_;
    uint64_t auth_revision_ = 0;
    
    // Thread safety (can be disabled for single-threaded use)
    mutable std::mutex mutex_;
};

} // namespace convex
