// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/events.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace fountainer {
namespace {

// Small slot container: unsubscribing during an ongoing delivery is
// allowed, which is why iteration happens over a copy of the handlers.
template <typename Signature>
class SlotList {
public:
    using Handler = std::function<Signature>;

    std::uint64_t add(Handler handler)
    {
        std::lock_guard lock(mutex_);
        const std::uint64_t token = ++next_token_;
        slots_.emplace(token, std::move(handler));
        return token;
    }

    void remove(std::uint64_t token)
    {
        std::lock_guard lock(mutex_);
        slots_.erase(token);
    }

    template <typename... Args>
    void emit(Args&&... args)
    {
        std::vector<Handler> snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot.reserve(slots_.size());
            for (const auto& [token, handler] : slots_) snapshot.push_back(handler);
        }
        for (auto& handler : snapshot) handler(args...);
    }

private:
    std::mutex mutex_;
    std::map<std::uint64_t, Handler> slots_;
    std::uint64_t next_token_ = 0;
};

}  // namespace

struct EventBus::Impl {
    SlotList<void(const ConnectionStateChange&)> connection_state;
    SlotList<void(const Heartbeat&)> heartbeat;
    SlotList<void(const DeviceAlert&)> device_alert;
    SlotList<void(const OtaStatus&)> ota_status;
    SlotList<void(const ErrorReport&)> error_report;
    SlotList<void(const ProtocolWarning&)> protocol_warning;
    SlotList<void(std::string_view, const nlohmann::json&)> unknown;
};

EventBus::EventBus() : impl_(std::make_shared<Impl>()) {}
EventBus::~EventBus() = default;

namespace {

// The token holds Impl only WEAKLY: if a subscription outlives the bus,
// unsubscribing becomes a no-op instead of a dangling access.
// (ImplT via deduction — the private type EventBus::Impl cannot be named
// here, but access via a template is permitted.)
template <typename ImplT, typename Slots, typename Handler>
Subscription attach(const std::shared_ptr<ImplT>& impl, Slots& slots,
                    Handler handler)
{
    const std::uint64_t token = slots.add(std::move(handler));
    return Subscription(
        [weak = std::weak_ptr<ImplT>(impl), slots_ptr = &slots, token] {
            if (auto alive = weak.lock()) slots_ptr->remove(token);
        });
}

}  // namespace

Subscription EventBus::on_connection_state(
    std::function<void(const ConnectionStateChange&)> handler)
{
    return attach(impl_, impl_->connection_state, std::move(handler));
}

Subscription EventBus::on_heartbeat(std::function<void(const Heartbeat&)> handler)
{
    return attach(impl_, impl_->heartbeat, std::move(handler));
}

Subscription EventBus::on_device_alert(std::function<void(const DeviceAlert&)> handler)
{
    return attach(impl_, impl_->device_alert, std::move(handler));
}

Subscription EventBus::on_ota_status(std::function<void(const OtaStatus&)> handler)
{
    return attach(impl_, impl_->ota_status, std::move(handler));
}

Subscription EventBus::on_error_report(std::function<void(const ErrorReport&)> handler)
{
    return attach(impl_, impl_->error_report, std::move(handler));
}

Subscription EventBus::on_protocol_warning(
    std::function<void(const ProtocolWarning&)> handler)
{
    return attach(impl_, impl_->protocol_warning, std::move(handler));
}

Subscription EventBus::on_unknown_message(
    std::function<void(std::string_view, const nlohmann::json&)> handler)
{
    return attach(impl_, impl_->unknown, std::move(handler));
}

void EventBus::publish(const ConnectionStateChange& event)
{
    impl_->connection_state.emit(event);
}
void EventBus::publish(const Heartbeat& event) { impl_->heartbeat.emit(event); }
void EventBus::publish(const DeviceAlert& event) { impl_->device_alert.emit(event); }
void EventBus::publish(const OtaStatus& event) { impl_->ota_status.emit(event); }
void EventBus::publish(const ErrorReport& event) { impl_->error_report.emit(event); }
void EventBus::publish(const ProtocolWarning& event)
{
    impl_->protocol_warning.emit(event);
}
void EventBus::publish_unknown(std::string_view type, const nlohmann::json& message)
{
    impl_->unknown.emit(type, message);
}

std::string_view to_string(ClientState state) noexcept
{
    switch (state) {
    case ClientState::Disconnected:         return "disconnected";
    case ClientState::Resolving:            return "resolving";
    case ClientState::TcpConnecting:        return "tcp_connecting";
    case ClientState::TlsHandshaking:       return "tls_handshaking";
    case ClientState::WebSocketHandshaking: return "websocket_handshaking";
    case ClientState::FountainHandshaking:  return "fountain_handshaking";
    case ClientState::Ready:                return "ready";
    case ClientState::Reconnecting:         return "reconnecting";
    case ClientState::Closing:              return "closing";
    }
    return "?";
}

}  // namespace fountainer
