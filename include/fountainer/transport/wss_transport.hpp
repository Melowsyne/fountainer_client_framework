// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// WSS transport (Boost.Beast + OpenSSL) as an ITextTransport implementation.
// Evolved from the former WssClient; the reconnect decision now lives
// one layer up (Client), no longer here.
//
// Threading: all methods and callbacks run on the executor with which
// the dialer was created (Client: a single IO thread).
#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <fountainer/endpoint.hpp>
#include <fountainer/security.hpp>
#include <fountainer/transport/text_transport.hpp>

namespace fountainer::transport {

struct WssOptions {
    std::chrono::seconds connect_timeout{10};

    // WS keepalive: Beast pings after half the time and closes when it
    // expires without traffic (0 = off). Needed because a hard-reset device
    // sends no FIN/RST (half-open TCP, observed live on 2026-08-14).
    // CAUTION: a WS ping does NOT count as activity for the firmware —
    // only the Fountain keepalive helps against its 300 s idle timeout.
    std::chrono::seconds idle_timeout{60};

    std::string user_agent = "fountainer/0.2";
};

// Creates a TLS context instance from TlsCredentials. Does not throw — the
// errors are returned as a Result so that connect() can report them.
[[nodiscard]] Result<std::shared_ptr<boost::asio::ssl::context>> make_ssl_context(
    const TlsCredentials& credentials);

class WssTransport final : public ITextTransport,
                           public ITransportDialer,
                           public std::enable_shared_from_this<WssTransport> {
public:
    static std::shared_ptr<WssTransport> create(
        boost::asio::any_io_executor executor,
        std::shared_ptr<boost::asio::ssl::context> ssl, Endpoint endpoint,
        EndpointIdentityPolicy identity_policy, WssOptions options);

    ~WssTransport() override;

    // --- ITransportDialer ---
    void async_open(OpenCompletion completion) override;
    void cancel() override;

    // --- ITextTransport ---
    void async_send(std::string frame, SendCompletion completion) override;
    void close(std::uint16_t code, std::string reason) override;
    void set_receive_handler(ReceiveHandler handler) override;
    void set_close_handler(CloseHandler handler) override;
    [[nodiscard]] bool is_open() const noexcept override;

private:
    using WsStream = boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    enum class Phase { Idle, Resolving, Connecting, TlsHandshake, WsHandshake,
                       Open, Closing, Closed };

    WssTransport(boost::asio::any_io_executor executor,
                 std::shared_ptr<boost::asio::ssl::context> ssl, Endpoint endpoint,
                 EndpointIdentityPolicy identity_policy, WssOptions options);

    void on_resolve(const boost::system::error_code& ec,
                    boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(const boost::system::error_code& ec,
                    const boost::asio::ip::tcp::endpoint& endpoint);
    void on_tls_handshake(const boost::system::error_code& ec);
    void on_ws_handshake(const boost::system::error_code& ec);
    void do_read();
    void on_read(const boost::system::error_code& ec, std::size_t bytes);
    void begin_write();
    void on_write(const boost::system::error_code& ec, std::size_t bytes);
    void on_close(const boost::system::error_code& ec);

    // Aborts the connection setup (calls the OpenCompletion) or terminates
    // an open connection (calls the CloseHandler) — exactly once in each case.
    void finish(Error error);
    void shutdown_socket();

    boost::asio::any_io_executor executor_;
    std::shared_ptr<boost::asio::ssl::context> ssl_;
    boost::asio::ip::tcp::resolver resolver_;
    WsStream ws_;
    boost::asio::steady_timer resolve_timer_;
    boost::beast::flat_buffer rx_buffer_;

    Endpoint endpoint_;
    EndpointIdentityPolicy identity_policy_;
    WssOptions options_;

    Phase phase_ = Phase::Idle;
    bool user_close_ = false;

    struct Outgoing {
        std::string frame;
        SendCompletion completion;
    };
    std::deque<Outgoing> tx_queue_;
    bool write_active_ = false;

    OpenCompletion open_completion_;
    ReceiveHandler receive_handler_;
    CloseHandler close_handler_;
};

}  // namespace fountainer::transport
