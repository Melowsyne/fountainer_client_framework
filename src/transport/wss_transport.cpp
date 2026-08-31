// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/transport/wss_transport.hpp"

#include <utility>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/system_error.hpp>

#include <openssl/ssl.h>

#include "fountainer/logging/logger.hpp"

namespace fountainer::transport {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;

namespace {

constexpr const char* kLogCat = "TRANSPORT";

bool is_ip_literal(const std::string& host)
{
    boost::system::error_code ec;
    asio::ip::make_address(host, ec);
    return !ec;
}

// A TLS error from the SSL category is practically always a certificate
// problem (wrong CA, rejected client certificate) — a quick retry does not
// help there. Everything else (timeout, reset, EOF) is temporary.
Error tls_error(const boost::system::error_code& ec)
{
    if (ec.category() == asio::error::get_ssl_category()) {
        return make_error(ErrorDomain::Tls, ErrorCode::CertificateRejected,
                          "TLS handshake failed: " + ec.message());
    }
    Error error = make_error(ErrorDomain::Tls, ErrorCode::TlsHandshakeFailed,
                             "TLS handshake failed: " + ec.message());
    error.retryable = true;
    return error;
}

Error ws_error(const boost::system::error_code& ec)
{
    if (ec == websocket::condition::handshake_failed) {
        // e.g. wrong path or no upgrade — a retry changes nothing.
        return make_error(ErrorDomain::WebSocket,
                          ErrorCode::WebSocketHandshakeFailed,
                          "WebSocket handshake rejected: " + ec.message());
    }
    Error error = make_error(ErrorDomain::WebSocket,
                             ErrorCode::WebSocketHandshakeFailed,
                             "WebSocket handshake failed: " + ec.message());
    error.retryable = true;
    return error;
}

}  // namespace

Result<std::shared_ptr<asio::ssl::context>> make_ssl_context(
    const TlsCredentials& credentials)
{
    if (auto status = credentials.validate(); !status) return fail(status.error());

    auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
    try {
        context->set_options(asio::ssl::context::default_workarounds |
                             asio::ssl::context::no_sslv2 |
                             asio::ssl::context::no_sslv3 |
                             asio::ssl::context::no_tlsv1 |
                             asio::ssl::context::no_tlsv1_1);

        // No interactive passphrase prompt: encrypted keys are deliberately
        // not supported and fail cleanly.
        context->set_password_callback(
            [](std::size_t, asio::ssl::context::password_purpose) {
                return std::string();
            });

        if (credentials.unsafe().disable_peer_verification) {
            context->set_verify_mode(asio::ssl::verify_none);
            log::warn(kLogCat, "peer verification is DISABLED (UnsafeTlsOptions)");
        } else {
            context->set_verify_mode(asio::ssl::verify_peer);
            context->load_verify_file(credentials.ca_file());
        }

        if (credentials.uses_mutual_tls()) {
            context->use_certificate_chain_file(credentials.certificate_file());
            context->use_private_key_file(credentials.key_file(),
                                          asio::ssl::context::pem);
            if (SSL_CTX_check_private_key(context->native_handle()) != 1) {
                return fail(config_error(
                    ErrorCode::MissingCredentials,
                    "client key does not match the client certificate"));
            }
        }
    } catch (const boost::system::system_error& e) {
        return fail(config_error(ErrorCode::FileNotReadable,
                                 std::string("TLS setup failed: ") + e.what()));
    }
    return context;
}

std::shared_ptr<WssTransport> WssTransport::create(
    asio::any_io_executor executor, std::shared_ptr<asio::ssl::context> ssl,
    Endpoint endpoint, EndpointIdentityPolicy identity_policy, WssOptions options)
{
    return std::shared_ptr<WssTransport>(
        new WssTransport(std::move(executor), std::move(ssl), std::move(endpoint),
                         identity_policy, std::move(options)));
}

WssTransport::WssTransport(asio::any_io_executor executor,
                           std::shared_ptr<asio::ssl::context> ssl,
                           Endpoint endpoint,
                           EndpointIdentityPolicy identity_policy,
                           WssOptions options)
    : executor_(std::move(executor)),
      ssl_(std::move(ssl)),
      resolver_(executor_),
      ws_(executor_, *ssl_),
      resolve_timer_(executor_),
      endpoint_(std::move(endpoint)),
      identity_policy_(identity_policy),
      options_(std::move(options))
{
}

WssTransport::~WssTransport() = default;

void WssTransport::async_open(OpenCompletion completion)
{
    if (phase_ != Phase::Idle) {
        if (completion) {
            completion(fail(internal_error("transport already used — create a "
                                           "fresh instance per attempt")));
        }
        return;
    }
    open_completion_ = std::move(completion);
    phase_ = Phase::Resolving;

    log::info(kLogCat, "connecting to " + endpoint_.to_string());

    // The Asio resolver has no timeout of its own.
    resolve_timer_.expires_after(options_.connect_timeout);
    resolve_timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code& ec) {
            if (!ec) self->resolver_.cancel();
        });

    resolver_.async_resolve(
        endpoint_.host, std::to_string(endpoint_.port),
        beast::bind_front_handler(&WssTransport::on_resolve, shared_from_this()));
}

void WssTransport::cancel()
{
    if (phase_ == Phase::Closed || phase_ == Phase::Closing) return;
    user_close_ = true;
    resolve_timer_.cancel();
    resolver_.cancel();
    finish(cancelled_error("connect"));
}

void WssTransport::on_resolve(const boost::system::error_code& ec,
                              asio::ip::tcp::resolver::results_type results)
{
    resolve_timer_.cancel();
    if (ec) {
        Error error = make_error(
            ErrorDomain::Dns, ErrorCode::ResolveFailed,
            (ec == asio::error::operation_aborted ? "DNS resolve timed out: "
                                                  : "DNS resolve failed: ") +
                ec.message());
        error.retryable = true;
        finish(std::move(error));
        return;
    }

    phase_ = Phase::Connecting;
    beast::get_lowest_layer(ws_).expires_after(options_.connect_timeout);
    beast::get_lowest_layer(ws_).async_connect(
        results,
        beast::bind_front_handler(&WssTransport::on_connect, shared_from_this()));
}

void WssTransport::on_connect(const boost::system::error_code& ec,
                              const asio::ip::tcp::endpoint& endpoint)
{
    if (ec) {
        Error error = make_error(ErrorDomain::Tcp, ErrorCode::ConnectFailed,
                                 "TCP connect failed: " + ec.message());
        error.retryable = true;
        finish(std::move(error));
        return;
    }

    log::debug(kLogCat, "TCP connected to " + endpoint.address().to_string() + ":" +
                            std::to_string(endpoint.port()));

    SSL* const handle = ws_.next_layer().native_handle();
    const bool host_is_ip = is_ip_literal(endpoint_.host);

    // SNI is not permitted for IP literals.
    if (!host_is_ip) {
        SSL_set_tlsext_host_name(handle, endpoint_.host.c_str());
    }

    if (identity_policy_ == EndpointIdentityPolicy::VerifyDnsName) {
        SSL_set1_host(handle, endpoint_.host.c_str());
    } else {
        log::debug(kLogCat, "certificate-chain-only policy for " + endpoint_.host);
    }

    phase_ = Phase::TlsHandshake;
    beast::get_lowest_layer(ws_).expires_after(options_.connect_timeout);
    ws_.next_layer().async_handshake(
        asio::ssl::stream_base::client,
        beast::bind_front_handler(&WssTransport::on_tls_handshake,
                                  shared_from_this()));
}

void WssTransport::on_tls_handshake(const boost::system::error_code& ec)
{
    if (ec) {
        finish(tls_error(ec));
        return;
    }
    log::debug(kLogCat, "TLS established");

    // From here on the WebSocket timeout is in charge, not the TCP stream.
    beast::get_lowest_layer(ws_).expires_never();

    websocket::stream_base::timeout timeouts{};
    timeouts.handshake_timeout = options_.connect_timeout;
    if (options_.idle_timeout > std::chrono::seconds::zero()) {
        timeouts.idle_timeout = options_.idle_timeout;
        timeouts.keep_alive_pings = true;
    } else {
        timeouts.idle_timeout = websocket::stream_base::none();
        timeouts.keep_alive_pings = false;
    }
    ws_.set_option(timeouts);
    ws_.read_message_max(kMaxRxFrameSize);
    ws_.set_option(websocket::stream_base::decorator(
        [subprotocol = endpoint_.subprotocol,
         agent = options_.user_agent](websocket::request_type& request) {
            request.set(http::field::user_agent, agent);
            if (!subprotocol.empty()) {
                request.set(http::field::sec_websocket_protocol, subprotocol);
            }
        }));

    phase_ = Phase::WsHandshake;
    ws_.async_handshake(
        endpoint_.host + ":" + std::to_string(endpoint_.port), endpoint_.path,
        beast::bind_front_handler(&WssTransport::on_ws_handshake,
                                  shared_from_this()));
}

void WssTransport::on_ws_handshake(const boost::system::error_code& ec)
{
    if (ec) {
        finish(ws_error(ec));
        return;
    }

    phase_ = Phase::Open;
    log::debug(kLogCat, "WebSocket open");

    if (auto completion = std::exchange(open_completion_, nullptr)) {
        completion(Result<TransportPtr>(shared_from_this()));
    }
    do_read();
}

void WssTransport::do_read()
{
    ws_.async_read(rx_buffer_, beast::bind_front_handler(&WssTransport::on_read,
                                                        shared_from_this()));
}

void WssTransport::on_read(const boost::system::error_code& ec, std::size_t bytes)
{
    if (ec == websocket::error::closed) {
        finish(make_error(ErrorDomain::Disconnected, ErrorCode::TransportClosed,
                        "peer closed the connection"));
        return;
    }
    if (ec) {
        if (user_close_) return;   // our own close() is in progress
        Error error = make_error(ErrorDomain::Disconnected,
                                 ErrorCode::TransportClosed,
                                 "read failed: " + ec.message());
        error.retryable = true;
        finish(std::move(error));
        return;
    }

    if (ws_.got_text()) {
        const auto data = rx_buffer_.cdata();
        const std::string_view frame(static_cast<const char*>(data.data()),
                                     data.size());
        if (receive_handler_) receive_handler_(frame);
    } else {
        log::warn(kLogCat, "binary frame ignored (" + std::to_string(bytes) +
                               " bytes)");
    }

    rx_buffer_.consume(rx_buffer_.size());
    if (phase_ == Phase::Open) do_read();
}

void WssTransport::async_send(std::string frame, SendCompletion completion)
{
    if (phase_ != Phase::Open) {
        if (completion) {
            completion(fail(make_error(ErrorDomain::Disconnected,
                                       ErrorCode::NotConnected,
                                       "transport is not open")));
        }
        return;
    }
    if (frame.size() > kMaxTxFrameSize) {
        // The firmware closes the session on oversized frames — rejecting
        // here is far friendlier than losing the session.
        if (completion) {
            completion(fail(make_error(
                ErrorDomain::Protocol, ErrorCode::FrameTooLarge,
                "frame of " + std::to_string(frame.size()) +
                    " bytes exceeds the device limit of " +
                    std::to_string(kMaxTxFrameSize))));
        }
        return;
    }
    if (tx_queue_.size() >= kMaxTxQueue) {
        if (completion) {
            completion(fail(make_error(ErrorDomain::RateLimit,
                                       ErrorCode::SendQueueFull,
                                       "transport send queue is full")));
        }
        return;
    }

    tx_queue_.push_back(Outgoing{std::move(frame), std::move(completion)});
    if (!write_active_) begin_write();
}

void WssTransport::begin_write()
{
    write_active_ = true;
    ws_.text(true);
    ws_.async_write(
        asio::buffer(tx_queue_.front().frame),
        beast::bind_front_handler(&WssTransport::on_write, shared_from_this()));
}

void WssTransport::on_write(const boost::system::error_code& ec, std::size_t)
{
    write_active_ = false;
    if (ec) {
        if (!tx_queue_.empty()) {
            auto pending = std::move(tx_queue_.front());
            tx_queue_.pop_front();
            if (pending.completion) {
                pending.completion(fail(make_error(ErrorDomain::Disconnected,
                                                   ErrorCode::TransportClosed,
                                                   "write failed: " + ec.message())));
            }
        }
        if (!user_close_) {
            Error error = make_error(ErrorDomain::Disconnected,
                                     ErrorCode::TransportClosed,
                                     "write failed: " + ec.message());
            error.retryable = true;
            finish(std::move(error));
        }
        return;
    }

    auto sent = std::move(tx_queue_.front());
    tx_queue_.pop_front();
    if (sent.completion) sent.completion(ok());

    if (!tx_queue_.empty() && phase_ == Phase::Open) begin_write();
}

void WssTransport::close(std::uint16_t code, std::string reason)
{
    if (phase_ == Phase::Closed || phase_ == Phase::Closing) return;
    user_close_ = true;

    if (phase_ != Phase::Open) {
        resolve_timer_.cancel();
        resolver_.cancel();
        finish(make_error(ErrorDomain::Disconnected, ErrorCode::TransportClosed,
                        "closed during connect: " + reason));
        return;
    }

    phase_ = Phase::Closing;
    websocket::close_reason close_reason(static_cast<websocket::close_code>(code));
    close_reason.reason = reason;
    ws_.async_close(close_reason, beast::bind_front_handler(&WssTransport::on_close,
                                                            shared_from_this()));
}

void WssTransport::on_close(const boost::system::error_code& ec)
{
    if (ec) log::debug(kLogCat, std::string("close finished with: ") + ec.message());
    finish(make_error(ErrorDomain::Disconnected, ErrorCode::TransportClosed,
                    "connection closed"));
}

void WssTransport::finish(Error error)
{
    const bool was_open = (phase_ == Phase::Open || phase_ == Phase::Closing);
    if (phase_ == Phase::Closed) return;
    phase_ = Phase::Closed;
    shutdown_socket();

    // Everything still in the send queue gets the same error.
    auto pending = std::exchange(tx_queue_, {});
    for (auto& item : pending) {
        if (item.completion) item.completion(fail(error));
    }

    // Before open, the error is the result of async_open; after that it is
    // the end of the connection — never both.
    if (auto completion = std::exchange(open_completion_, nullptr)) {
        completion(Result<TransportPtr>(unexpected_t{std::move(error)}));
        return;
    }
    if (was_open) {
        if (auto handler = std::exchange(close_handler_, nullptr)) {
            handler(std::move(error));
        }
    }
}

void WssTransport::shutdown_socket()
{
    boost::system::error_code ignored;
    beast::get_lowest_layer(ws_).socket().close(ignored);
}

void WssTransport::set_receive_handler(ReceiveHandler handler)
{
    receive_handler_ = std::move(handler);
}

void WssTransport::set_close_handler(CloseHandler handler)
{
    close_handler_ = std::move(handler);
}

bool WssTransport::is_open() const noexcept { return phase_ == Phase::Open; }

}  // namespace fountainer::transport
