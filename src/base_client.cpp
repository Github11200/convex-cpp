#include "convex/base_client.hpp"
#include "convex/protocol.hpp"
#include "convex/udf_path.hpp"
#include "request_manager.hpp"
#include <sstream>

namespace convex {

BaseConvexClient::BaseConvexClient(OutgoingMode mode)
    : requests_(std::make_unique<detail::RequestManager>()),
      queue_messages_(mode == OutgoingMode::QueueMessages) {}
BaseConvexClient::~BaseConvexClient() = default;

void BaseConvexClient::enqueue_locked(const ClientMessage& message) {
    if (queue_messages_) outgoing_messages_.push(message);
}

std::optional<ClientMessage> BaseConvexClient::pop_next_message() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outgoing_messages_.empty()) return std::nullopt;
    auto message = std::move(outgoing_messages_.front());
    outgoing_messages_.pop();
    return message;
}

std::string BaseConvexClient::make_query_token(
    const std::string& udf_path,
    const Object& args
) const {
    // Create a unique token for query deduplication
    std::ostringstream oss;
    oss << udf_path << ":";
    oss << serialize_args(args);
    return oss.str();
}

// ============================================================================
// Connection Management
// ============================================================================

ClientMessage BaseConvexClient::connect_message(
    const SessionId& session_id,
    uint32_t connection_count,
    const std::string& close_reason
) {
    ConnectMessage msg;
    msg.session_id = session_id;
    msg.connection_count = connection_count;
    msg.last_close_reason = close_reason;
    msg.max_observed_timestamp = max_observed_timestamp();
    msg.client_ts = 0;  // We don't track client timestamps for now
    return msg;
}

std::vector<ClientMessage> BaseConvexClient::reconnect_messages() {
    AuthCallback fetcher;
    uint64_t auth_revision;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fetcher = auth_fetcher_;
        auth_revision = auth_revision_;
    }
    std::optional<AuthenticationToken> refreshed;
    if (fetcher) {
        try { refreshed = fetcher(true); }
        catch (...) { /* Keep the cached token when refresh fails. */ }
    }
    std::unique_lock<std::mutex> lock(mutex_);
    std::vector<ClientMessage> messages;
    std::vector<detail::RequestManager::Completion> failures;
    remote_version_ = {};
    remote_results_.clear();
    identity_version_ = 0;
    if (refreshed && auth_revision == auth_revision_) current_token_ = std::move(*refreshed);
    if (current_token_.type != AuthenticationToken::Type::None) {
        messages.emplace_back(AuthenticateMessage{identity_version_++, current_token_});
    }
    {
        ModifyQuerySetMessage msg;
        msg.base_version = 0;
        msg.new_version = query_set_version_ = 1;
        
        for (const auto& [token, local_query] : query_set_) {
            QuerySetModification mod;
            mod.type = QuerySetModification::Type::Add;
            mod.query_id = local_query.id;
            
            Query q;
            q.query_id = local_query.id;
            q.udf_path = local_query.udf_path;
            q.args_json = serialize_args(local_query.args);
            mod.query = q;
            
            msg.modifications.push_back(std::move(mod));
        }
        
        messages.emplace_back(std::move(msg));
    }
    requests_->restart(auth_changed_at_, messages, failures);
    outgoing_messages_ = {};
    for (const auto& message : messages) enqueue_locked(message);
    lock.unlock();
    for (const auto& failure : failures) failure.notify();
    return messages;
}

// ============================================================================
// Query Subscriptions
// ============================================================================

std::pair<SubscriberId, std::optional<ClientMessage>> BaseConvexClient::subscribe(
    const std::string& udf_path,
    const Object& args,
    QueryCallback callback
) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto canonical_path = UdfPath(udf_path).canonicalize();
    std::string token = make_query_token(canonical_path, args);
    
    // Check if already subscribed to this query
    auto it = query_set_.find(token);
    if (it != query_set_.end()) {
        // Add another subscriber to existing query
        LocalQuery& lq = it->second;
        lq.num_subscribers++;
        lq.subscription_index++;
        
        SubscriberId sub_id{lq.id, lq.subscription_index};
        subscribers_.insert(sub_id);
        subscriber_callbacks_[sub_id] = std::move(callback);
        
        // If we already have a result, call the callback immediately
        auto result_it = latest_results_.find(lq.id);
        if (result_it != latest_results_.end() && subscriber_callbacks_[sub_id]) {
            auto notify = subscriber_callbacks_[sub_id];
            auto result = result_it->second;
            lock.unlock();
            notify(sub_id, result);
        }
        
        return {sub_id, std::nullopt};
    }
    
    // New subscription
    QueryId query_id = next_query_id_++;
    QuerySetVersion base_version = query_set_version_;
    QuerySetVersion new_version = ++query_set_version_;
    
    LocalQuery lq;
    lq.id = query_id;
    lq.udf_path = std::move(canonical_path);
    lq.args = args;
    lq.num_subscribers = 1;
    lq.subscription_index = 0;
    
    query_set_[token] = lq;
    query_id_to_token_[query_id] = token;
    
    SubscriberId sub_id{query_id, 0};
    subscribers_.insert(sub_id);
    subscriber_callbacks_[sub_id] = std::move(callback);
    
    // Create the ModifyQuerySet message
    ModifyQuerySetMessage msg;
    msg.base_version = base_version;
    msg.new_version = new_version;
    
    QuerySetModification mod;
    mod.type = QuerySetModification::Type::Add;
    mod.query_id = query_id;
    
    Query q;
    q.query_id = query_id;
    q.udf_path = udf_path;
    q.args_json = serialize_args(args);
    mod.query = q;
    
    msg.modifications.push_back(std::move(mod));
    
    enqueue_locked(msg);
    return {sub_id, msg};
}

std::optional<ClientMessage> BaseConvexClient::unsubscribe(const SubscriberId& subscriber_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto sub_it = subscribers_.find(subscriber_id);
    if (sub_it == subscribers_.end()) {
        return std::nullopt;  // Already unsubscribed
    }
    
    subscribers_.erase(sub_it);
    subscriber_callbacks_.erase(subscriber_id);
    
    // Find the query
    auto token_it = query_id_to_token_.find(subscriber_id.query_id);
    if (token_it == query_id_to_token_.end()) {
        return std::nullopt;
    }
    
    auto query_it = query_set_.find(token_it->second);
    if (query_it == query_set_.end()) {
        return std::nullopt;
    }
    
    LocalQuery& lq = query_it->second;
    lq.num_subscribers--;
    
    // If still has subscribers, don't remove from server
    if (lq.num_subscribers > 0) {
        return std::nullopt;
    }
    
    // Remove the query
    QueryId query_id = lq.id;
    std::string token = token_it->second;
    
    query_set_.erase(query_it);
    query_id_to_token_.erase(token_it);
    latest_results_.erase(query_id);
    
    // Create Remove message
    QuerySetVersion base_version = query_set_version_;
    QuerySetVersion new_version = ++query_set_version_;
    
    ModifyQuerySetMessage msg;
    msg.base_version = base_version;
    msg.new_version = new_version;
    
    QuerySetModification mod;
    mod.type = QuerySetModification::Type::Remove;
    mod.query_id = query_id;
    msg.modifications.push_back(mod);
    
    enqueue_locked(msg);
    return msg;
}

std::optional<FunctionResult> BaseConvexClient::get_result(const SubscriberId& subscriber_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (subscribers_.count(subscriber_id) == 0) {
        return std::nullopt;
    }
    
    auto it = latest_results_.find(subscriber_id.query_id);
    if (it == latest_results_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

QueryResults BaseConvexClient::snapshot_locked() const {
    QueryResults snapshot;
    for (const auto& id : subscribers_) {
        auto it = latest_results_.find(id.query_id);
        if (it == latest_results_.end()) snapshot.entries_.emplace(id, std::nullopt);
        else snapshot.entries_.emplace(id, it->second);
    }
    return snapshot;
}

QueryResults BaseConvexClient::latest_results() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_locked();
}

// ============================================================================
// Mutations and Actions
// ============================================================================

ClientMessage BaseConvexClient::mutation(
    const std::string& udf_path,
    const Object& args,
    MutationCallback callback
) {
    UdfPath path(udf_path);
    std::lock_guard<std::mutex> lock(mutex_);
    
    SessionRequestSeqNumber request_id = next_request_id_++;
    
    MutationMessage msg;
    msg.request_id = request_id;
    msg.udf_path = path.to_string();
    msg.args_json = serialize_args(args);
    requests_->track(request_id, detail::RequestManager::Type::Mutation, msg, std::move(callback));
    enqueue_locked(msg);
    
    return msg;
}

ClientMessage BaseConvexClient::action(
    const std::string& udf_path,
    const Object& args,
    ActionCallback callback
) {
    UdfPath path(udf_path);
    std::lock_guard<std::mutex> lock(mutex_);
    
    SessionRequestSeqNumber request_id = next_request_id_++;
    
    ActionMessage msg;
    msg.request_id = request_id;
    msg.udf_path = path.to_string();
    msg.args_json = serialize_args(args);
    requests_->track(request_id, detail::RequestManager::Type::Action, msg, std::move(callback));
    enqueue_locked(msg);
    
    return msg;
}

// ============================================================================
// Authentication
// ============================================================================

ClientMessage BaseConvexClient::set_auth_fetcher(AuthCallback fetcher) {
    auto token = AuthenticationToken::none();
    if (fetcher) {
        try { token = fetcher(false); }
        catch (...) { /* An initial fetch failure signs out. */ }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auth_fetcher_ = std::move(fetcher);
    ++auth_revision_;
    auth_changed_at_ = next_request_id_;
    current_token_ = std::move(token);
    AuthenticateMessage message{identity_version_++, current_token_};
    enqueue_locked(message);
    return message;
}

ClientMessage BaseConvexClient::authenticate(const AuthenticationToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    current_token_ = token;
    auth_fetcher_ = {};
    ++auth_revision_;
    auth_changed_at_ = next_request_id_;
    
    AuthenticateMessage msg;
    msg.base_version = identity_version_++;
    msg.token = token;
    
    enqueue_locked(msg);
    return msg;
}

ClientMessage BaseConvexClient::clear_auth() {
    return authenticate(AuthenticationToken::none());
}

// ============================================================================
// Message Handling
// ============================================================================

std::optional<QueryResults> BaseConvexClient::handle_message(const ServerMessage& message) {
    struct QueryUpdate {
        SubscriberId id;
        QueryCallback callback;
        FunctionResult result;
    };
    std::vector<QueryUpdate> updates;
    std::vector<detail::RequestManager::Completion> completions;
    std::optional<QueryResults> snapshot;
    std::unique_lock<std::mutex> lock(mutex_);
    auto observe = [this](Timestamp ts) {
        if (!max_observed_ts_ || ts > *max_observed_ts_) max_observed_ts_ = ts;
    };
    std::visit([&](const auto& msg) {
        using T = std::decay_t<decltype(msg)>;
        
        if constexpr (std::is_same_v<T, TransitionMessage>) {
            observe(msg.end_version.ts);
            if (msg.start_version != remote_version_) throw std::runtime_error("StartVersionMismatch");
            remote_version_ = msg.end_version;
            completions = requests_->complete_transition();
            std::set<QueryId> changed;
            for (const auto& mod : msg.modifications) {
                FunctionResult result;
                
                switch (mod.type) {
                    case StateModification::Type::QueryUpdated:
                        result = FunctionResult::success(
                            mod.value.value_or(Value())
                        );
                        break;
                        
                    case StateModification::Type::QueryFailed:
                        if (mod.error_data) {
                            result = FunctionResult::convex_error(
                                mod.error_message.value_or("Query failed"),
                                *mod.error_data
                            );
                        } else {
                            result = FunctionResult::error(
                                mod.error_message.value_or("Query failed")
                            );
                        }
                        break;
                        
                    case StateModification::Type::QueryRemoved:
                        remote_results_.erase(mod.query_id);
                        changed.erase(mod.query_id);
                        continue;
                }
                
                auto previous = remote_results_.find(mod.query_id);
                if (previous == remote_results_.end() || previous->second != result) {
                    remote_results_[mod.query_id] = std::move(result);
                    changed.insert(mod.query_id);
                }
            }
            for (auto id : changed) {
                if (query_id_to_token_.count(id)) latest_results_[id] = remote_results_.at(id);
            }
            for (const auto& [id, callback] : subscriber_callbacks_) {
                if (callback && changed.count(id.query_id)) {
                    updates.push_back({id, callback, latest_results_.at(id.query_id)});
                }
            }
            snapshot = snapshot_locked();
        }
        else if constexpr (std::is_same_v<T, MutationResponseMessage> || std::is_same_v<T, ActionResponseMessage>) {
            using Type = detail::RequestManager::Type;
            constexpr bool mutation = std::is_same_v<T, MutationResponseMessage>;
            std::optional<Timestamp> ts;
            if constexpr (mutation) {
                ts = msg.ts;
                if (ts) observe(*ts);
            }
            auto result = msg.success ? FunctionResult::success(msg.result.value_or(Value()))
                : !msg.error ? FunctionResult::error("Request failed")
                : msg.error->data ? FunctionResult::convex_error(msg.error->message, *msg.error->data)
                : FunctionResult::error(msg.error->message);
            auto completion = requests_->update(msg.request_id, mutation ? Type::Mutation : Type::Action,
                                                std::move(result), ts);
            if (completion) completions.push_back(std::move(*completion));
        }
        else if constexpr (std::is_same_v<T, AuthErrorMessage>) {
            throw std::runtime_error("AuthError: " + msg.error_message);
        }
        else if constexpr (std::is_same_v<T, FatalErrorMessage>) {
            throw std::runtime_error("FatalError: " + msg.error_message);
        }
    }, message);
    lock.unlock();
    for (const auto& update : updates) update.callback(update.id, update.result);
    for (const auto& completion : completions) completion.notify();
    return snapshot;
}

} // namespace convex
