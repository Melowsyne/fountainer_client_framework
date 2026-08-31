// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/protocol/dispatcher.hpp"

#include <algorithm>
#include <utility>

#include "fountainer/protocol/fountain_messages.hpp"

namespace fountainer::protocol {

std::string_view to_string(OperationPriority priority) noexcept
{
    switch (priority) {
    case OperationPriority::SafetyControl:      return "safety_control";
    case OperationPriority::InteractiveControl: return "interactive_control";
    case OperationPriority::InteractiveRead:    return "interactive_read";
    case OperationPriority::LogTransfer:        return "log_transfer";
    case OperationPriority::Polling:            return "polling";
    case OperationPriority::Background:         return "background";
    }
    return "?";
}

RequestDispatcher::RequestDispatcher(EncodeAndSend send, RateBudget budget,
                                     std::chrono::milliseconds default_timeout)
    : send_(std::move(send)),
      budget_(budget),
      default_timeout_(default_timeout),
      tokens_(budget.burst)
{
}

void RequestDispatcher::set_warning_handler(WarningHandler handler)
{
    warning_ = std::move(handler);
}

void RequestDispatcher::complete(std::vector<ResponseHandler>& handlers,
                                 const Result<nlohmann::json>& result)
{
    // Take a copy and clear BEFORE calling: a handler may submit new
    // requests from within the callback.
    auto taken = std::move(handlers);
    handlers.clear();
    for (auto& handler : taken) {
        if (handler) handler(result);
    }
}

void RequestDispatcher::submit(RequestSpec spec, ResponseHandler handler,
                               TimePoint now)
{
    metrics_.submitted++;
    const auto timeout = spec.timeout.value_or(default_timeout_);

    // Coalescing: attach to an identical, not yet sent request instead of
    // creating a second one (design concept §13.7).
    if (!spec.coalesce_key.empty()) {
        const auto it = std::find_if(
            queue_.begin(), queue_.end(), [&](const Waiting& waiting) {
                return waiting.spec.coalesce_key == spec.coalesce_key;
            });
        if (it != queue_.end()) {
            it->handlers.push_back(std::move(handler));
            // The more urgent priority wins.
            if (spec.priority < it->spec.priority) it->spec.priority = spec.priority;
            it->deadline = std::max(it->deadline, now + timeout);
            metrics_.coalesced++;
            return;
        }
    }

    Waiting waiting;
    waiting.deadline = now + timeout;
    waiting.sequence = ++sequence_;
    waiting.handlers.push_back(std::move(handler));
    waiting.spec = std::move(spec);
    queue_.push_back(std::move(waiting));

    // Try immediately — with free budget a request costs no latency.
    tick(now);
}

void RequestDispatcher::refill(TimePoint now)
{
    if (!refill_initialised_) {
        last_refill_ = now;
        refill_initialised_ = true;
        return;
    }
    const auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
    if (elapsed <= 0.0) return;
    last_refill_ = now;
    tokens_ = std::min(budget_.burst,
                       tokens_ + elapsed * budget_.requests_per_second);
}

void RequestDispatcher::drain(TimePoint now)
{
    if (!sending_enabled_ || draining_) return;

    // A handler may submit new requests from within the callback; the outer
    // drain() picks them up afterwards (no recursive sending).
    draining_ = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{draining_};

    while (!queue_.empty() && pending_.size() < budget_.max_in_flight) {
        if (tokens_ < 1.0) {
            metrics_.rate_limit_deferrals++;
            return;
        }

        // Strict priority, FIFO within a priority.
        const auto next = std::min_element(
            queue_.begin(), queue_.end(), [](const Waiting& a, const Waiting& b) {
                if (a.spec.priority != b.spec.priority) {
                    return a.spec.priority < b.spec.priority;
                }
                return a.sequence < b.sequence;
            });

        Waiting waiting = std::move(*next);
        queue_.erase(next);

        auto sent = send_(waiting.spec);
        if (!sent) {
            metrics_.send_failures++;
            const Result<nlohmann::json> failure{unexpected_t{sent.error()}};
            complete(waiting.handlers, failure);
            continue;
        }

        tokens_ -= 1.0;
        metrics_.sent++;
        Pending pending;
        pending.type = waiting.spec.type;
        pending.handlers = std::move(waiting.handlers);
        // The deadline remains the total budget from submit(); time spent
        // waiting in the queue does NOT extend it (the application was
        // promised a response time, not a send time).
        pending.deadline = waiting.deadline;
        pending_.emplace(std::move(*sent), std::move(pending));
        (void)now;
    }
}

void RequestDispatcher::tick(TimePoint now)
{
    refill(now);

    // 1) Waiting requests whose budget expired before they were even sent.
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->deadline <= now) {
            metrics_.timeouts++;
            const Result<nlohmann::json> failure{
                unexpected_t{timeout_error(it->spec.type)}};
            complete(it->handlers, failure);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }

    // 2) Open requests without a response.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.deadline <= now) {
            metrics_.timeouts++;
            auto handlers = std::move(it->second.handlers);
            const std::string type = it->second.type;
            it = pending_.erase(it);
            const Result<nlohmann::json> failure{unexpected_t{timeout_error(type)}};
            complete(handlers, failure);
        } else {
            ++it;
        }
    }

    drain(now);
}

bool RequestDispatcher::on_message(const nlohmann::json& message)
{
    const std::string in_reply_to = message.value("in_reply_to", std::string{});
    if (in_reply_to.empty()) return false;

    const auto it = pending_.find(in_reply_to);
    if (it == pending_.end()) {
        // Late straggler after a timeout — not an error, but make it
        // visible.
        if (warning_) {
            warning_("response for an unknown or expired request",
                     message.value("type", std::string{}));
        }
        return true;   // consumed: it is definitely not a spontaneous event
    }

    const std::string type = message.value("type", std::string{});
    const MessageMeta* request_meta = find_meta(it->second.type);

    auto handlers = std::move(it->second.handlers);
    const std::string request_type = it->second.type;
    pending_.erase(it);

    if (request_meta != nullptr && !request_meta->accepts_response(type)) {
        // The response type is enforced (design concept §4 D) — otherwise
        // wrongly correlated messages slip unnoticed into the domain layer.
        metrics_.unexpected_responses++;
        if (warning_) warning_("unexpected response type for " + request_type, type);
        Error error = protocol_error(ErrorCode::UnexpectedResponseType,
                                     request_type + " was answered with '" + type +
                                         "'");
        error.operation = request_type;
        const Result<nlohmann::json> failure{unexpected_t{std::move(error)}};
        complete(handlers, failure);
        return true;
    }

    metrics_.completed++;
    const Result<nlohmann::json> success{message};
    complete(handlers, success);
    return true;
}

void RequestDispatcher::fail_all(const Error& error)
{
    auto pending = std::exchange(pending_, {});
    auto queue = std::exchange(queue_, {});

    metrics_.disconnect_failures += pending.size() + queue.size();

    const Result<nlohmann::json> failure{unexpected_t{error}};
    for (auto& [msg_id, entry] : pending) {
        (void)msg_id;
        complete(entry.handlers, failure);
    }
    for (auto& entry : queue) {
        complete(entry.handlers, failure);
    }
}

DispatcherMetrics RequestDispatcher::metrics() const
{
    DispatcherMetrics out = metrics_;
    out.in_flight = pending_.size();
    out.queued = queue_.size();
    return out;
}

}  // namespace fountainer::protocol
