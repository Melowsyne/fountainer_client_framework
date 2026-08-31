// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Transport abstraction (design concept §22). The protocol core knows
// neither DNS nor TLS nor sockets — only "text frame in, text frame out".
//
// Implementations:
//   WssTransport           PC -> ESP32   (dialer, this repo)
//   AcceptedWssTransport   ESP32 -> backend (later, same core)
//   FakeTransport          tests
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <fountainer/result.hpp>

namespace fountainer::transport {

// The firmware enforces LOCAL_MAX_FRAME_SIZE = 4096 for INCOMING frames
// (local_buffer_pool.h) — larger TX frames kill the session.
inline constexpr std::size_t kMaxTxFrameSize = 4096;

// Log batches may reach up to ~24 KB of payload (task_com.c) — with
// envelope/escaping, 64 KiB is the safe upper bound (design concept §16.3).
inline constexpr std::size_t kMaxRxFrameSize = 64 * 1024;

inline constexpr std::size_t kMaxTxQueue = 100;

// WebSocket close codes used by the protocol layer.
inline constexpr std::uint16_t kCloseNormal = 1000;
inline constexpr std::uint16_t kCloseProtocolError = 4000;
inline constexpr std::uint16_t kCloseAuthFailed = 4004;

class ITextTransport {
public:
    using ReceiveHandler = std::function<void(std::string_view)>;
    // Exactly ONCE per opened transport. The Error describes why it was
    // closed (domain Disconnected = regular end).
    using CloseHandler = std::function<void(Error)>;
    using SendCompletion = std::function<void(Status)>;

    virtual ~ITextTransport() = default;

    // completion may be empty ("fire and forget"). On rejection (frame too
    // large, queue full, closed) it is called with an error.
    virtual void async_send(std::string frame, SendCompletion completion) = 0;

    virtual void close(std::uint16_t code, std::string reason) = 0;

    virtual void set_receive_handler(ReceiveHandler handler) = 0;
    virtual void set_close_handler(CloseHandler handler) = 0;

    [[nodiscard]] virtual bool is_open() const noexcept = 0;
};

using TransportPtr = std::shared_ptr<ITextTransport>;

// Establishes a connection. There is no dialer for accepted backend
// connections — there the transport already exists.
class ITransportDialer {
public:
    using OpenCompletion = std::function<void(Result<TransportPtr>)>;

    virtual ~ITransportDialer() = default;

    // Exactly one attempt in progress per dialer instance.
    virtual void async_open(OpenCompletion completion) = 0;

    // Aborts an attempt in progress; completion is called with
    // Cancelled.
    virtual void cancel() = 0;
};

}  // namespace fountainer::transport
