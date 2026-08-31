// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Error model of the framework (design concept §19). Network errors are
// expected runtime states and are carried as VALUES, not as exceptions.
//
// IMPORTANT: a remote rejection ("status":"rejected") is NOT an Error —
// the transport succeeded. It shows up as a WriteResult/CommandResult
// with applied() == false.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fountainer {

// Coarse classification for the application's retry/escalation decisions.
enum class ErrorDomain {
    Configuration,
    Dns,
    Tcp,
    Tls,
    WebSocket,
    Protocol,
    Authentication,
    Validation,
    Timeout,
    Disconnected,
    RateLimit,
    Remote,
    Cancelled,
    Internal,
};

enum class ErrorCode {
    // Configuration
    MissingCredentials,
    InvalidEndpoint,
    InvalidOption,
    FileNotReadable,
    // Dns/Tcp/Tls/WebSocket
    ResolveFailed,
    ConnectFailed,
    TlsHandshakeFailed,
    CertificateRejected,
    WebSocketHandshakeFailed,
    TransportClosed,
    FrameTooLarge,
    SendQueueFull,
    // Protocol
    ProtocolMismatch,
    UnexpectedMessage,
    UnexpectedResponseType,
    MalformedMessage,
    HandshakeFailed,
    // Authentication
    AuthRejected,
    ReplayDetected,
    UnknownKeyId,
    // Validation
    UnknownDatapoint,
    ReadOnlyDatapoint,
    TypeMismatch,
    OutOfRange,
    ValueTooLong,
    ConstraintViolation,
    EmptySelection,
    // Runtime
    RequestTimeout,
    Disconnected,
    NotConnected,
    InvalidState,
    Cancelled,
    RateLimited,
    DeviceBusy,
    // Miscellaneous
    RemoteRejected,
    NotSupported,
    Internal,
};

std::string_view to_string(ErrorDomain domain) noexcept;
std::string_view to_string(ErrorCode code) noexcept;

struct Error {
    ErrorDomain domain = ErrorDomain::Internal;
    ErrorCode code = ErrorCode::Internal;
    std::string message;

    // May the operation be retried without user intervention?
    bool retryable = false;

    // Context so that errors stay readable without log correlation.
    std::optional<std::string> operation;      // "dp_read", "connect", ...
    std::optional<std::string> datapoint;      // affected datapoint
    std::optional<std::string> remote_detail;  // the device's wording

    // One-liner for logs/CLI: "timeout/request_timeout: dp_read (Fon_...)"
    std::string to_string() const;
};

// --- Factories for the common cases -----------------------------------------

Error make_error(ErrorDomain domain, ErrorCode code, std::string message);

Error config_error(ErrorCode code, std::string message);
Error validation_error(ErrorCode code, std::string message,
                       std::string datapoint = {});
Error timeout_error(std::string operation);
Error disconnected_error(std::string operation);
Error cancelled_error(std::string operation);
Error protocol_error(ErrorCode code, std::string message);
Error internal_error(std::string message);

}  // namespace fountainer
