#pragma once

#include "convex/client.hpp"

namespace convex::detail {

struct QueryBroadcast {
    std::mutex mutex;
    uint64_t sequence = 0;
    QueryResults latest;
};

// Owns a single protocol session, shared by all copies of ConvexClient. A
// caller drives the worker with poll()/run(), independently of the transport.
class ClientWorker : public std::enable_shared_from_this<ClientWorker> {
public:
    using ResultCallback = std::function<void(const FunctionResult&)>;
    using StateCallback = std::function<void(ConnectionState)>;

    ClientWorker(const std::string& deployment_url, std::unique_ptr<ITransport> transport,
                 std::string client_id, StateCallback state_change);
    ~ClientWorker();

    void connect();
    void disconnect();
    bool is_connected() const;
    void on_connection_change(StateCallback callback);
    Subscription subscribe(const std::string& udf_path, const Object& args, ResultCallback callback);
    void unsubscribe(const SubscriberId& id);
    std::optional<FunctionResult> current(const SubscriberId& id) const;
    QuerySetSubscription watch_all() const;
    void mutation_async(const std::string& udf_path, const Object& args, ResultCallback callback);
    void action_async(const std::string& udf_path, const Object& args, ResultCallback callback);
    void set_auth_callback(AuthCallback fetcher);
    FunctionResult wait_for_result(const std::function<void(ResultCallback)>& start);
    void poll();
    void run();
    void stop();

private:
    void drain_transport_events();
    void handle_open();
    void handle_message(const std::string& msg);
    void handle_close(const std::string& reason);
    void handle_error(const std::string& error);
    void attempt_reconnect();
    void process(int timeout_ms);
    static std::string deployment_to_ws_url(const std::string& deployment_url);

    std::string ws_url_;
    std::string client_id_;
    std::unique_ptr<ITransport> transport_;
    BaseConvexClient base_client_{BaseConvexClient::OutgoingMode::QueueMessages};
    std::shared_ptr<QueryBroadcast> broadcast_ = std::make_shared<QueryBroadcast>();

    std::string session_id_;
    uint32_t connection_count_ = 0;
    std::string last_close_reason_ = "InitialConnect";
    Backoff backoff_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_reconnect_{true};
    std::optional<std::chrono::steady_clock::time_point> reconnect_at_;
    StateCallback on_connection_change_;

    // ESP-IDF events can come from a different task. Only the driver
    // consumes these events and calls back into the transport.
    enum class EventType { Open, Message, Close, Error };
    void enqueue_event(EventType type, std::string data = {});
    std::queue<std::pair<EventType, std::string>> transport_events_;
    std::mutex event_mutex_;
};

} // namespace convex::detail
