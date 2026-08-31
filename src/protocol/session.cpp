// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/protocol/session.hpp"

#include <algorithm>
#include <utility>

#include <openssl/rand.h>

#include "fountainer/logging/logger.hpp"
#include "fountainer/protocol/envelope.hpp"

namespace fountainer::protocol {

namespace {

constexpr const char* kLogCat = "PROTOCOL";

std::string random_nonce_b64()
{
    unsigned char raw[16];
    if (RAND_bytes(raw, sizeof raw) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(24);
    for (int i = 0; i < 15; i += 3) {
        const unsigned v = (raw[i] << 16) | (raw[i + 1] << 8) | raw[i + 2];
        out += alphabet[(v >> 18) & 0x3f];
        out += alphabet[(v >> 12) & 0x3f];
        out += alphabet[(v >> 6) & 0x3f];
        out += alphabet[v & 0x3f];
    }
    const unsigned v = raw[15] << 16;   // 1 remaining byte -> "XX=="
    out += alphabet[(v >> 18) & 0x3f];
    out += alphabet[(v >> 12) & 0x3f];
    out += "==";
    return out;
}

std::optional<float> optional_float(const nlohmann::json& message, const char* key)
{
    const auto it = message.find(key);
    if (it == message.end() || !it->is_number()) return std::nullopt;
    return it->get<float>();
}

}  // namespace

std::string_view to_string(SessionState state) noexcept
{
    switch (state) {
    case SessionState::Closed:     return "closed";
    case SessionState::AwaitHello: return "await_hello";
    case SessionState::AwaitProof: return "await_proof";
    case SessionState::Running:    return "running";
    }
    return "?";
}

ControllerSession::ControllerSession(ControllerSessionConfig config, SendFn send,
                                     CloseFn close, NowFn now)
    : config_(std::move(config)),
      send_(std::move(send)),
      close_(std::move(close)),
      now_(now ? std::move(now) : NowFn([] { return Clock::now(); })),
      dispatcher_([this](const RequestSpec& spec) { return encode_and_send(spec); },
                  config_.budget, config_.default_request_timeout)
{
    if (!config_.ota_policy) config_.ota_policy = no_update_policy();
    dispatcher_.set_sending_enabled(false);
    dispatcher_.set_warning_handler(
        [this](std::string reason, std::string type) {
            warn(std::move(reason), std::move(type));
        });
}

void ControllerSession::set_callbacks(Callbacks callbacks)
{
    callbacks_ = std::move(callbacks);
}

void ControllerSession::enter(SessionState next)
{
    if (state_ == next) return;
    log::debug(kLogCat, std::string("session ") + std::string(to_string(state_)) +
                            " -> " + std::string(to_string(next)));
    state_ = next;
    dispatcher_.set_sending_enabled(next == SessionState::Running);
}

void ControllerSession::warn(std::string reason, std::string type,
                             std::optional<std::string> in_reply_to)
{
    log::debug(kLogCat, reason + " (type=" + type + ")");
    if (callbacks_.on_warning) {
        callbacks_.on_warning(ProtocolWarning{std::move(reason), std::move(type),
                                              std::move(in_reply_to)});
    }
}

void ControllerSession::on_transport_open()
{
    // Fresh nonce and counters per connection — anti-replay depends on them.
    auth_ = AuthContext{{}, config_.kid, {}, random_nonce_b64(), ""};
    replay_ = AntiReplay{};
    s2c_seq_ = 0;
    msg_seq_ = 0;
    info_ = ConnectionInfo{};
    failed_ = false;
    last_tx_ = now_();
    handshake_deadline_ = now_() + config_.handshake_timeout;
    enter(SessionState::AwaitHello);
}

void ControllerSession::on_transport_closed(const Error& error)
{
    enter(SessionState::Closed);
    dispatcher_.fail_all(error);
}

void ControllerSession::abort(Error error, std::uint16_t close_code,
                              const std::string& reason)
{
    if (failed_) return;
    failed_ = true;
    log::warn(kLogCat, "closing session: " + reason + " (" + error.to_string() + ")");
    enter(SessionState::Closed);
    dispatcher_.fail_all(error);
    if (close_) close_(close_code, reason);
    if (callbacks_.on_failed) callbacks_.on_failed(std::move(error));
}

std::string ControllerSession::next_msg_id()
{
    return "s-" + std::to_string(++msg_seq_);
}

bool ControllerSession::transmit(const nlohmann::json& message)
{
    if (!send_) return false;
    if (!send_(message.dump())) return false;
    last_tx_ = now_();
    return true;
}

bool ControllerSession::send_message(std::string_view type,
                                     const nlohmann::json& body,
                                     const std::string& in_reply_to)
{
    nlohmann::json message = build_message(type, body, "", in_reply_to, now_ms());
    const MessageMeta* meta = find_meta(type);
    if (meta != nullptr && meta->auth == AuthLevel::Control) {
        s2c_seq_++;
        sign(message, auth_, s2c_seq_, Direction::S2c);
    }
    return transmit(message);
}

Result<std::string> ControllerSession::encode_and_send(const RequestSpec& spec)
{
    if (state_ != SessionState::Running) {
        return fail(make_error(ErrorDomain::Disconnected, ErrorCode::NotConnected,
                               "session is " + std::string(to_string(state_)) +
                                   ", not running"));
    }
    const MessageMeta* meta = find_meta(spec.type);
    if (meta == nullptr) {
        return fail(protocol_error(ErrorCode::UnexpectedMessage,
                                   "unknown message type '" + spec.type + "'"));
    }

    const std::string msg_id = next_msg_id();
    nlohmann::json message = build_message(spec.type, spec.body, msg_id, "", now_ms());
    if (meta->auth == AuthLevel::Control) {
        s2c_seq_++;
        sign(message, auth_, s2c_seq_, Direction::S2c);
    }

    const std::string encoded = message.dump();
    if (encoded.size() > kMaxControlFrameBytes) {
        // The firmware closes the session on oversized frames; this is a
        // real application limit, not a transport detail.
        return fail(make_error(ErrorDomain::Protocol, ErrorCode::FrameTooLarge,
                               spec.type + " frame is " +
                                   std::to_string(encoded.size()) +
                                   " bytes, device limit is " +
                                   std::to_string(kMaxControlFrameBytes)));
    }
    if (!send_ || !send_(encoded)) {
        return fail(make_error(ErrorDomain::Disconnected, ErrorCode::TransportClosed,
                               spec.type + " could not be sent"));
    }
    last_tx_ = now_();
    return msg_id;
}

void ControllerSession::request(RequestSpec spec, ResponseHandler handler)
{
    dispatcher_.submit(std::move(spec), std::move(handler), now_());
}

void ControllerSession::tick(TimePoint now)
{
    if ((state_ == SessionState::AwaitHello || state_ == SessionState::AwaitProof) &&
        now >= handshake_deadline_) {
        abort(make_error(ErrorDomain::Protocol, ErrorCode::HandshakeFailed,
                         "no Fountain handshake within the deadline"),
              4000, "handshake_timeout");
        return;
    }
    dispatcher_.tick(now);
}

void ControllerSession::on_text(std::string_view frame)
{
    nlohmann::json message =
        nlohmann::json::parse(frame, nullptr, /*allow_exceptions=*/false);
    if (!message.is_object()) {
        // In the handshake garbage is fatal (foreign client); when running, just drop.
        if (state_ == SessionState::Running) {
            warn("invalid JSON discarded", "");
            return;
        }
        abort(protocol_error(ErrorCode::MalformedMessage,
                             "peer sent something that is not a JSON object"),
              4000, "invalid_json");
        return;
    }

    switch (state_) {
    case SessionState::AwaitHello: handle_hello(message); break;
    case SessionState::AwaitProof: handle_proof(message); break;
    case SessionState::Running:    dispatch_running(message); break;
    case SessionState::Closed:     break;   // ignore after close
    }
}

void ControllerSession::handle_hello(const nlohmann::json& message)
{
    const std::string type = message.value("type", std::string{});
    if (type == "protocol_mismatch") {
        abort(protocol_error(ErrorCode::ProtocolMismatch,
                             "device reports no common protocol version"),
              4000, "protocol_mismatch");
        return;
    }
    if (type != "hello") {
        abort(protocol_error(ErrorCode::UnexpectedMessage,
                             "expected hello, got '" + type + "'"),
              4000, "expected_hello");
        return;
    }

    info_.device_id = message.value("device_id", std::string{});
    info_.serial = message.value("serial", std::string{});
    info_.firmware_version = message.value("fw_version", std::string{});
    info_.hardware_revision = message.value("hw_rev", std::string{});
    info_.boot_reason = message.value("boot_reason", std::string{});
    info_.protocol_version =
        static_cast<std::uint8_t>(message.value("protocol_version", 0));

    log::info(kLogCat, "hello from " + info_.device_id + " (serial " + info_.serial +
                           ", fw " + info_.firmware_version + ", protocol v" +
                           std::to_string(info_.protocol_version) + ")");

    auth_.client_nonce = message.value("client_nonce", std::string{});

    const auto schemes = message.value("auth_schemes", nlohmann::json::array());
    const auto kids = message.value("auth_kids", nlohmann::json::array());
    const bool scheme_ok =
        std::any_of(schemes.begin(), schemes.end(), [](const nlohmann::json& s) {
            return s.is_string() && s.get<std::string>() == kAuthScheme;
        });

    // Which of the KIDs offered by the device can we resolve?
    std::string chosen_kid;
    HmacKey key;
    Error resolve_error =
        make_error(ErrorDomain::Authentication, ErrorCode::UnknownKeyId,
                   "device offered no key id we can resolve");
    if (config_.credentials) {
        for (const auto& entry : kids) {
            if (!entry.is_string()) continue;
            const std::string candidate = entry.get<std::string>();
            auto resolved = config_.credentials->resolve(info_.device_id, candidate);
            if (resolved) {
                chosen_kid = candidate;
                key = std::move(*resolved);
                break;
            }
            resolve_error = resolved.error();
        }
    } else {
        resolve_error = config_error(ErrorCode::MissingCredentials,
                                     "no credential provider configured");
    }

    if (!config_.expected_device_id.empty() &&
        info_.device_id != config_.expected_device_id) {
        send_message("hello_ack",
                     {{"accepted", false},
                      {"reason", "unknown_device"},
                      {"supported_protocols", {1, 2}},
                      {"server_ts", now_ms()}},
                     message.value("msg_id", std::string{}));
        abort(make_error(ErrorDomain::Authentication, ErrorCode::AuthRejected,
                         "device identifies as '" + info_.device_id +
                             "', expected '" + config_.expected_device_id + "'"),
              4004, "unknown_device");
        return;
    }

    if (!scheme_ok || chosen_kid.empty()) {
        send_message("hello_ack",
                     {{"accepted", false},
                      {"reason", "auth_required"},
                      {"supported_protocols", {1, 2}},
                      {"server_ts", now_ms()}},
                     message.value("msg_id", std::string{}));
        abort(std::move(resolve_error), 4004, "auth_required");
        return;
    }

    auth_.key = key.bytes();
    auth_.kid = chosen_kid;
    auth_.device_id = info_.device_id;
    info_.auth_scheme = std::string(kAuthScheme);
    info_.auth_scope = std::string(kAuthScope);
    info_.auth_kid = chosen_kid;

    send_message("hello_ack",
                 {{"accepted", true},
                  {"supported_protocols", {1, 2}},
                  {"server_ts", now_ms()},
                  {"auth_required", true},
                  {"auth_scheme", std::string(kAuthScheme)},
                  {"auth_scope", std::string(kAuthScope)},
                  {"auth_kid", chosen_kid},
                  {"server_nonce", auth_.server_nonce}},
                 message.value("msg_id", std::string{}));
    enter(SessionState::AwaitProof);
}

void ControllerSession::handle_proof(const nlohmann::json& message)
{
    const VerifyResult result = verify(message, auth_, Direction::C2s);
    if (!result.ok) {
        abort(make_error(ErrorDomain::Authentication, ErrorCode::AuthRejected,
                         "session proof failed: " + result.reason),
              4004, "auth_failed");
        return;
    }
    if (!replay_.check(message["auth"].value("seq", std::int64_t{0}))) {
        abort(make_error(ErrorDomain::Authentication, ErrorCode::ReplayDetected,
                         "session proof replayed"),
              4004, "auth_failed");
        return;
    }

    info_.established_at = now_();
    log::info(kLogCat, "session established (device=" + info_.device_id +
                           ", kid=" + info_.auth_kid + ", fw=" +
                           info_.firmware_version + ")");

    if (message.value("type", std::string{}) == kSessionProofMsg) {
        answer_ota_check(message);
        enter(SessionState::Running);
        if (callbacks_.on_ready) callbacks_.on_ready(info_);
    } else {
        // Parity with the reference implementation: the first signed frame
        // may also be of another type — it has already been verified.
        enter(SessionState::Running);
        if (callbacks_.on_ready) callbacks_.on_ready(info_);
        dispatch_running(message);
    }
}

void ControllerSession::answer_ota_check(const nlohmann::json& message)
{
    OtaCheck check;
    check.current_version = message.value("current_version", std::string{});
    check.hw_rev = message.value("hw_rev", std::string{});

    const OtaDecision decision = config_.ota_policy->check(info_, check);
    const std::string in_reply_to = message.value("msg_id", std::string{});

    if (const auto* available = std::get_if<OtaAvailable>(&decision)) {
        log::info(kLogCat, "offering OTA " + available->version);
        send_message("ota_available",
                     {{"version", available->version},
                      {"url", available->url},
                      {"sha256", available->sha256},
                      {"size", available->size}},
                     in_reply_to);
        return;
    }
    send_message("ota_none", nlohmann::json::object(), in_reply_to);
}

void ControllerSession::dispatch_running(const nlohmann::json& message)
{
    const std::string type = message.value("type", std::string{});

    // Verify signed inbound messages; a failure discards the message but
    // does NOT kill the session (telemetry must not hinge on a single frame).
    if (message.contains("auth")) {
        const VerifyResult result = verify(message, auth_, Direction::C2s);
        if (!result.ok) {
            warn("dropping message: " + result.reason, type);
            return;
        }
        if (!replay_.check(message["auth"].value("seq", std::int64_t{0}))) {
            warn("dropping message: replay", type);
            return;
        }
    }

    if (dispatcher_.on_message(message)) return;

    if (type == "heartbeat") {
        if (callbacks_.on_heartbeat) {
            Heartbeat heartbeat;
            heartbeat.uptime_s = message.value("uptime_s", std::uint32_t{0});
            heartbeat.firmware_version = message.value("fw_version", std::string{});
            heartbeat.fault_active = message.value("fault_active", false);
            heartbeat.received_at = now_();
            callbacks_.on_heartbeat(heartbeat);
        }
        return;
    }

    if (type == "dp_report") {
        if (callbacks_.on_dp_report) {
            std::optional<std::uint32_t> seq;
            if (const auto it = message.find("seq");
                it != message.end() && it->is_number()) {
                seq = it->get<std::uint32_t>();
            }
            callbacks_.on_dp_report(message.value("dp", nlohmann::json::object()),
                                    seq);
        }
        return;
    }

    if (type == "device_alert") {
        if (callbacks_.on_device_alert) {
            DeviceAlert alert;
            alert.code = message.value("code", std::string{});
            alert.severity = message.value("severity", std::string{});
            alert.datapoint = message.value("datapoint", std::string{});
            alert.value = optional_float(message, "value");
            alert.threshold = optional_float(message, "threshold");
            alert.detail = message.value("detail", std::string{});
            alert.received_at = now_();
            callbacks_.on_device_alert(alert);
        }
        return;
    }

    if (type == "ota_status") {
        if (callbacks_.on_ota_status) {
            OtaStatus status;
            status.target_version = message.value("target_version", std::string{});
            status.state = message.value("state", std::string{});
            status.attempt = static_cast<std::uint8_t>(message.value("attempt", 0));
            status.progress_pct =
                static_cast<std::uint8_t>(message.value("progress_pct", 0));
            status.error = message.value("error", std::string{});
            status.error_detail = message.value("error_detail", std::string{});
            callbacks_.on_ota_status(status);
        }
        return;
    }

    if (type == "error_report") {
        if (callbacks_.on_error_report) {
            ErrorReport report;
            report.status = message.value("status", std::string{});
            report.active_faults =
                message.value("active_faults", nlohmann::json::array());
            report.log = message.value("log", nlohmann::json::array());
            callbacks_.on_error_report(report);
        }
        return;
    }

    if (find_meta(type) == nullptr) {
        // Forward compatibility: do NOT treat unknown types as errors.
        if (callbacks_.on_unknown) callbacks_.on_unknown(type, message);
        return;
    }

    warn("unsolicited message without a pending request", type);
}

}  // namespace fountainer::protocol
