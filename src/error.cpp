// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/error.hpp"

#include <utility>

namespace fountainer {

std::string_view to_string(ErrorDomain domain) noexcept
{
    switch (domain) {
    case ErrorDomain::Configuration:  return "configuration";
    case ErrorDomain::Dns:            return "dns";
    case ErrorDomain::Tcp:            return "tcp";
    case ErrorDomain::Tls:            return "tls";
    case ErrorDomain::WebSocket:      return "websocket";
    case ErrorDomain::Protocol:       return "protocol";
    case ErrorDomain::Authentication: return "authentication";
    case ErrorDomain::Validation:     return "validation";
    case ErrorDomain::Timeout:        return "timeout";
    case ErrorDomain::Disconnected:   return "disconnected";
    case ErrorDomain::RateLimit:      return "rate_limit";
    case ErrorDomain::Remote:         return "remote";
    case ErrorDomain::Cancelled:      return "cancelled";
    case ErrorDomain::Internal:       return "internal";
    }
    return "internal";
}

std::string_view to_string(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::MissingCredentials:       return "missing_credentials";
    case ErrorCode::InvalidEndpoint:          return "invalid_endpoint";
    case ErrorCode::InvalidOption:            return "invalid_option";
    case ErrorCode::FileNotReadable:          return "file_not_readable";
    case ErrorCode::ResolveFailed:            return "resolve_failed";
    case ErrorCode::ConnectFailed:            return "connect_failed";
    case ErrorCode::TlsHandshakeFailed:       return "tls_handshake_failed";
    case ErrorCode::CertificateRejected:      return "certificate_rejected";
    case ErrorCode::WebSocketHandshakeFailed: return "websocket_handshake_failed";
    case ErrorCode::TransportClosed:          return "transport_closed";
    case ErrorCode::FrameTooLarge:            return "frame_too_large";
    case ErrorCode::SendQueueFull:            return "send_queue_full";
    case ErrorCode::ProtocolMismatch:         return "protocol_mismatch";
    case ErrorCode::UnexpectedMessage:        return "unexpected_message";
    case ErrorCode::UnexpectedResponseType:   return "unexpected_response_type";
    case ErrorCode::MalformedMessage:         return "malformed_message";
    case ErrorCode::HandshakeFailed:          return "handshake_failed";
    case ErrorCode::AuthRejected:             return "auth_rejected";
    case ErrorCode::ReplayDetected:           return "replay_detected";
    case ErrorCode::UnknownKeyId:             return "unknown_key_id";
    case ErrorCode::UnknownDatapoint:         return "unknown_datapoint";
    case ErrorCode::ReadOnlyDatapoint:        return "read_only";
    case ErrorCode::TypeMismatch:             return "type_mismatch";
    case ErrorCode::OutOfRange:               return "out_of_range";
    case ErrorCode::ValueTooLong:             return "too_long";
    case ErrorCode::ConstraintViolation:      return "constraint_violation";
    case ErrorCode::EmptySelection:           return "empty_selection";
    case ErrorCode::RequestTimeout:           return "request_timeout";
    case ErrorCode::Disconnected:             return "disconnected";
    case ErrorCode::NotConnected:             return "not_connected";
    case ErrorCode::InvalidState:             return "invalid_state";
    case ErrorCode::Cancelled:                return "cancelled";
    case ErrorCode::RateLimited:              return "rate_limited";
    case ErrorCode::DeviceBusy:               return "device_busy";
    case ErrorCode::RemoteRejected:           return "remote_rejected";
    case ErrorCode::NotSupported:             return "not_supported";
    case ErrorCode::Internal:                 return "internal";
    }
    return "internal";
}

std::string Error::to_string() const
{
    std::string out(fountainer::to_string(domain));
    out += '/';
    out += fountainer::to_string(code);
    if (!message.empty()) {
        out += ": ";
        out += message;
    }
    if (operation) out += " [op=" + *operation + "]";
    if (datapoint) out += " [dp=" + *datapoint + "]";
    if (remote_detail) out += " [remote=" + *remote_detail + "]";
    return out;
}

namespace {

// Only domains where a blind retry makes sense. Validation/
// Configuration/Authentication deliberately stay false — they require a
// change by the application.
bool default_retryable(ErrorDomain domain)
{
    switch (domain) {
    case ErrorDomain::Dns:
    case ErrorDomain::Tcp:
    case ErrorDomain::Timeout:
    case ErrorDomain::Disconnected:
    case ErrorDomain::RateLimit:
        return true;
    default:
        return false;
    }
}

}  // namespace

Error make_error(ErrorDomain domain, ErrorCode code, std::string message)
{
    Error e;
    e.domain = domain;
    e.code = code;
    e.message = std::move(message);
    e.retryable = default_retryable(domain);
    return e;
}

Error config_error(ErrorCode code, std::string message)
{
    return make_error(ErrorDomain::Configuration, code, std::move(message));
}

Error validation_error(ErrorCode code, std::string message,
                       std::string datapoint)
{
    Error e = make_error(ErrorDomain::Validation, code, std::move(message));
    if (!datapoint.empty()) e.datapoint = std::move(datapoint);
    return e;
}

Error timeout_error(std::string operation)
{
    Error e = make_error(ErrorDomain::Timeout, ErrorCode::RequestTimeout,
                         "no response within the request deadline");
    e.operation = std::move(operation);
    return e;
}

Error disconnected_error(std::string operation)
{
    Error e = make_error(ErrorDomain::Disconnected, ErrorCode::Disconnected,
                         "connection lost while the request was pending");
    e.operation = std::move(operation);
    return e;
}

Error cancelled_error(std::string operation)
{
    Error e = make_error(ErrorDomain::Cancelled, ErrorCode::Cancelled,
                         "request cancelled");
    e.operation = std::move(operation);
    return e;
}

Error protocol_error(ErrorCode code, std::string message)
{
    return make_error(ErrorDomain::Protocol, code, std::move(message));
}

Error internal_error(std::string message)
{
    return make_error(ErrorDomain::Internal, ErrorCode::Internal,
                      std::move(message));
}

}  // namespace fountainer
