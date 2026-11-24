#pragma once

#include "convex/base_client.hpp"
#include <algorithm>
#include <stdexcept>

namespace convex::detail {

// Like the Rust manager, callers update the state first and notify waiters
// separately. Callbacks may reenter the client.
class RequestManager {
public:
    enum class Type { Mutation, Action };

    struct Completion {
        SessionRequestSeqNumber id;
        MutationCallback callback;
        FunctionResult result;
        void notify() const { if (callback) callback(id, result); }
    };

    void track(SessionRequestSeqNumber id, Type type, ClientMessage message, MutationCallback callback) {
        pending_.emplace(id, Request{type, std::move(message), std::move(callback), {}, {}});
    }

    std::optional<Completion> update(SessionRequestSeqNumber id, Type type,
                                     FunctionResult result, std::optional<Timestamp> ts) {
        auto it = pending_.find(id);
        if (it == pending_.end()) throw std::runtime_error("Invalid request id from server");
        if (it->second.type != type) throw std::runtime_error("Mismatched request type from server");
        it->second.result = std::move(result);
        it->second.ts = ts;
        if (type == Type::Action || it->second.result->type == FunctionResult::Type::Error) {
            return take(it);
        }
        return std::nullopt;
    }

    std::vector<Completion> complete_transition() {
        std::vector<Completion> completions;
        for (auto it = pending_.begin(); it != pending_.end();) {
            auto current = it++;
            if (current->second.result) completions.push_back(take(current));
        }
        return completions;
    }

    void restart(SessionRequestSeqNumber auth_changed_at, std::vector<ClientMessage>& messages,
                 std::vector<Completion>& failures) {
        std::vector<const Request*> replay;
        for (auto it = pending_.begin(); it != pending_.end();) {
            auto current = it++;
            auto& request = current->second;
            if (current->first < auth_changed_at && !request.result) {
                request.result = FunctionResult::error("Authentication changed before the request completed");
                failures.push_back(take(current));
            } else {
                replay.push_back(&request);
            }
        }
        std::stable_sort(replay.begin(), replay.end(), [](auto* a, auto* b) { return a->ts > b->ts; });
        for (auto* request : replay) messages.push_back(request->message);
    }

private:
    struct Request {
        Type type;
        ClientMessage message;
        MutationCallback callback;
        std::optional<FunctionResult> result;
        std::optional<Timestamp> ts;
    };
    std::map<SessionRequestSeqNumber, Request> pending_;

    Completion take(decltype(pending_)::iterator it) {
        Completion completion{it->first, std::move(it->second.callback), std::move(*it->second.result)};
        pending_.erase(it);
        return completion;
    }
};

} // namespace convex::detail
