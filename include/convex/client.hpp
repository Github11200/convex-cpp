#pragma once

#include "base_client.hpp"
#include "transport.hpp"
#include "platform/platform_detect.hpp"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>
#include <utility>

namespace convex {

namespace detail {
struct QueryBroadcast;
class ClientWorker;
}

class QuerySetSubscription {
public:
    QuerySetSubscription() = default;
    // Returns the latest unread transaction snapshot, if one is available.
    std::optional<QueryResults> next();

private:
    friend class detail::ClientWorker;
    explicit QuerySetSubscription(const std::shared_ptr<detail::QueryBroadcast>& broadcast);
    std::weak_ptr<detail::QueryBroadcast> broadcast_;
    uint64_t sequence_ = 0;
};

/**
 * Query subscription handle.
 * Automatically unsubscribes when destroyed.
 */
class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    ~Subscription();
    
    /**
     * Get the current result (if available)
     */
    std::optional<FunctionResult> current() const;
    const SubscriberId& id() const { return id_; }
    std::optional<FunctionResult> next();
    
    /**
     * Check if subscription is active
     */
    bool is_active() const { return !worker_.expired(); }
    
    /**
     * Explicitly unsubscribe
     */
    void unsubscribe();
    
private:
    friend class detail::ClientWorker;
    Subscription(std::weak_ptr<detail::ClientWorker> worker, SubscriberId id,
                 QuerySetSubscription watch, std::optional<FunctionResult> initial);

    std::weak_ptr<detail::ClientWorker> worker_;
    SubscriberId id_{};
    QuerySetSubscription watch_;
    std::optional<FunctionResult> initial_;
};

/**
 * ConvexClient - High-level client for interacting with Convex.
 * 
 * This class manages:
 * - WebSocket connection with automatic reconnection
 * - Cooperative message processing (`poll()` or `run()`)
 * - Query subscriptions with real-time updates
 * - Mutation and action requests
 * - Authentication
 * 
 * Example usage:
 * ```cpp
 * ConvexClient client("https://your-app.convex.cloud");
 * 
 * auto sub = client.subscribe("messages:list", {}, [](const FunctionResult& result) {
 *     if (result.is_success()) std::cout << result.value.to_string() << '\n';
 * });
 * 
 * client.mutation("messages:send", {
 *     {"body", "Hello!"},
 *     {"author", "C++"}
 * });
 * 
 * client.run();  // Run until stopped
 * ```
 */
class ConvexClient {
public:
    /**
     * Construct a client connected to the given deployment URL
     * @param deployment_url The Convex deployment URL (e.g., "https://your-app.convex.cloud")
     */
    explicit ConvexClient(const std::string& deployment_url);
    
    /**
     * Construct with custom transport (for testing or special platforms)
     */
    ConvexClient(const std::string& deployment_url, std::unique_ptr<ITransport> transport);
    
    // Copies share one connection, worker and subscription set.
    ConvexClient(const ConvexClient&) = default;
    ConvexClient& operator=(const ConvexClient&) = default;
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * Connect to the Convex server.
     * Usually called automatically, but can be called explicitly.
     */
    void connect();
    
    /**
     * Disconnect from the server
     */
    void disconnect();
    
    /**
     * Check if connected
     */
    bool is_connected() const;
    
    /**
     * Set callback for connection state changes
     */
    void on_connection_change(std::function<void(ConnectionState)> callback);
    
    // ========================================================================
    // Query Subscriptions
    // ========================================================================
    
    /**
     * Subscribe to a query with real-time updates
     * @param udf_path Path to the query function (e.g., "messages:list")
     * @param args Query arguments
     * @param on_update Called whenever the query result changes
     * @return Subscription handle (unsubscribes when destroyed)
     */
    Subscription subscribe(
        const std::string& udf_path,
        const Object& args,
        std::function<void(const FunctionResult&)> on_update = {}
    );
    QuerySetSubscription watch_all() const;
    
    /**
     * One-shot query (subscribes, gets result, unsubscribes)
     * Blocks until result is available.
     */
    FunctionResult query(
        const std::string& udf_path,
        const Object& args
    );
    
    // ========================================================================
    // Mutations and Actions
    // ========================================================================
    
    /**
     * Call a mutation (async)
     * @param udf_path Path to the mutation function
     * @param args Mutation arguments
     * @param on_complete Called when mutation completes
     */
    void mutation_async(
        const std::string& udf_path,
        const Object& args,
        std::function<void(const FunctionResult&)> on_complete
    );
    
    /**
     * Call a mutation (blocking)
     * Blocks until mutation completes.
     */
    FunctionResult mutation(
        const std::string& udf_path,
        const Object& args
    );
    
    /**
     * Call an action (async)
     */
    void action_async(
        const std::string& udf_path,
        const Object& args,
        std::function<void(const FunctionResult&)> on_complete
    );
    
    /**
     * Call an action (blocking)
     */
    FunctionResult action(
        const std::string& udf_path,
        const Object& args
    );
    
    // ========================================================================
    // Authentication
    // ========================================================================
    
    /**
     * Set authentication token (e.g., JWT from Auth0/Clerk)
     */
    void set_auth(std::optional<std::string> token);
    void set_auth_callback(AuthCallback fetcher);
    void set_admin_auth(std::string deploy_key, std::optional<Object> acting_as = {});
    
    /**
     * Clear authentication
     */
    void clear_auth();
    
    /**
     * Set auth token fetcher for automatic refresh
     */
    void set_auth_fetcher(std::function<std::string(bool force_refresh)> fetcher);
    
    // ========================================================================
    // Event Loop
    // ========================================================================
    
    /**
     * Drive one iteration if not using run(). A connection attempt or
     * transport I/O can block depending on the platform.
     */
    void poll();
    
    /**
     * Run the event loop (blocking)
     * Processes events until stop() is called.
     */
    void run();
    
    /**
     * Stop the event loop
     */
    void stop();
    
private:
    friend class ConvexClientBuilder;

    ConvexClient(const std::string& deployment_url, std::unique_ptr<ITransport> transport,
                 std::string client_id, std::function<void(ConnectionState)> state_change);
    std::shared_ptr<detail::ClientWorker> worker_;
};

using QuerySubscription = Subscription;

class ConvexClientBuilder {
public:
    explicit ConvexClientBuilder(std::string deployment_url) : deployment_url_(std::move(deployment_url)) {}
    ConvexClientBuilder& with_client_id(std::string client_id) {
        client_id_ = std::move(client_id);
        return *this;
    }
    ConvexClientBuilder& with_on_state_change(std::function<void(ConnectionState)> callback) {
        state_change_ = std::move(callback);
        return *this;
    }
    ConvexClient build() const;
    ConvexClient build(std::unique_ptr<ITransport> transport) const;

private:
    std::string deployment_url_;
    std::string client_id_ = "cpp-convex/0.1.0";
    std::function<void(ConnectionState)> state_change_;
};

} // namespace convex
