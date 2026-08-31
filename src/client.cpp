// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/client.hpp"

#include <atomic>
#include <future>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "fountainer/detail/sync_wait.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include "fountainer/logging/logger.hpp"
#include "fountainer/protocol/session.hpp"
#include "fountainer/transport/wss_transport.hpp"

namespace fountainer {

namespace asio = boost::asio;

namespace {

constexpr const char* kLogCat = "CLIENT";

// Drives session deadlines and the poller; 100 ms is fine enough for 250 ms
// realtime polling and coarse enough not to be noticeable when idle.
constexpr std::chrono::milliseconds kTickInterval{100};

}  // namespace

// ---------------------------------------------------------------------------
// RawProtocol
// ---------------------------------------------------------------------------

void RawProtocol::bind(SubmitFn submit, SendFn send, BlockingRunner runner)
{
    submit_ = std::move(submit);
    send_ = std::move(send);
    runner_ = std::move(runner);
}

void RawProtocol::send(std::string type, nlohmann::json body)
{
    send_(std::move(type), std::move(body));
}

void RawProtocol::async_request(protocol::RequestSpec spec,
                                protocol::ResponseHandler handler)
{
    submit_(std::move(spec), std::move(handler));
}

Result<nlohmann::json> RawProtocol::request(protocol::RequestSpec spec)
{
    return detail::sync_wait<nlohmann::json>(
        runner_,
        [this, spec = std::move(spec)](
            std::function<void(Result<nlohmann::json>)> done) mutable {
            submit_(std::move(spec), std::move(done));
        });
}

// ---------------------------------------------------------------------------
// Client::Impl
// ---------------------------------------------------------------------------

struct Client::Impl {
    Endpoint endpoint;
    ClientOptions options{};
    SecurityConfiguration security{};
    std::shared_ptr<protocol::OtaPolicy> ota_policy;

    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard;
    std::thread io_thread;
    std::thread::id io_thread_id;

    std::shared_ptr<transport::WssTransport> transport;   // open connection
    std::shared_ptr<transport::WssTransport> dialer;      // attempt in progress
    std::unique_ptr<protocol::ControllerSession> session;
    asio::steady_timer tick_timer;
    asio::steady_timer reconnect_timer;

    std::atomic<ClientState> state{ClientState::Disconnected};
    mutable std::mutex info_mutex;
    std::optional<ConnectionInfo> info;

    std::vector<ConnectCompletion> connect_completions;    // IO thread only
    std::vector<std::function<void()>> disconnect_waiters;  // IO thread only
    std::chrono::seconds current_backoff{0};
    bool user_disconnect = false;
    bool was_ready = false;               // reconnect only after first Ready
    std::optional<Error> session_failure; // last handshake/auth cause
    std::mt19937 rng{std::random_device{}()};

    DatapointManager datapoints;
    DatapointPoller poller;
    CommandService commands;
    LogService logs;
    MaintenanceService maintenance;
    EventBus events;
    RawProtocol raw;

    explicit Impl(Endpoint ep)
        : endpoint(std::move(ep)),
          work_guard(asio::make_work_guard(io)),
          tick_timer(io),
          reconnect_timer(io),
          poller(datapoints)
    {
        std::promise<std::thread::id> thread_id;
        io_thread = std::thread([this, &thread_id] {
            thread_id.set_value(std::this_thread::get_id());
            io.run();
        });
        io_thread_id = thread_id.get_future().get();
        wire_services();
    }

    ~Impl()
    {
        shutdown();
        work_guard.reset();
        io.stop();
        if (io_thread.joinable()) io_thread.join();
    }

    // ------------------------------------------------------------------
    // Service wiring
    // ------------------------------------------------------------------

    void wire_services()
    {
        auto submit = [this](protocol::RequestSpec spec,
                             protocol::ResponseHandler handler) {
            asio::post(io, [this, spec = std::move(spec),
                            handler = std::move(handler)]() mutable {
                if (!session) {
                    handler(fail(make_error(ErrorDomain::Disconnected,
                                            ErrorCode::NotConnected,
                                            "client is not connected")));
                    return;
                }
                session->request(std::move(spec), std::move(handler));
            });
        };

        auto runner = [this](std::function<void()> work) -> Status {
            if (std::this_thread::get_id() == io_thread_id) {
                return fail(make_error(
                    ErrorDomain::Internal, ErrorCode::InvalidState,
                    "blocking API called from the client IO thread — use the "
                    "async_* variants inside callbacks"));
            }
            asio::post(io, std::move(work));
            return ok();
        };

        auto send = [this](std::string type, nlohmann::json body) {
            asio::post(io, [this, type = std::move(type), body = std::move(body)] {
                if (session) session->send_message(type, body);
            });
        };

        datapoints.bind(submit, runner);
        commands.bind(submit, runner);
        logs.bind(submit, runner, datapoints);
        maintenance.bind(datapoints, commands,
                         [this] { return options.enable_test_commands; });
        raw.bind(submit, std::move(send), runner);
    }

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    void enter_state(ClientState next, std::optional<ConnectionInfo> ready_info,
                     std::optional<Error> cause)
    {
        const ClientState previous = state.exchange(next);
        if (previous == next) return;
        log::debug(kLogCat, std::string(to_string(previous)) + " -> " +
                                std::string(to_string(next)));
        ConnectionStateChange change;
        change.previous = previous;
        change.current = next;
        change.info = std::move(ready_info);
        change.cause = std::move(cause);
        events.publish(change);
    }

    // ------------------------------------------------------------------
    // Connection setup (IO thread)
    // ------------------------------------------------------------------

    void begin_connect(ConnectCompletion completion)
    {
        const ClientState current = state.load();

        // R1: already connected ⇒ answer immediately instead of silently
        // queueing the completion (it would never have fired -> deadlock).
        if (current == ClientState::Ready) {
            if (completion) {
                std::optional<ConnectionInfo> current_info;
                {
                    std::lock_guard lock(info_mutex);
                    current_info = info;
                }
                completion(Result<ConnectionInfo>(std::move(*current_info)));
            }
            return;
        }
        if (current == ClientState::Closing) {
            if (completion) {
                completion(fail(make_error(ErrorDomain::Internal,
                                           ErrorCode::InvalidState,
                                           "client is closing")));
            }
            return;
        }

        if (completion) connect_completions.push_back(std::move(completion));
        if (current != ClientState::Disconnected &&
            current != ClientState::Reconnecting) {
            return;   // attempt in progress; completion rides on the same round
        }
        // A manual connect() during the backoff replaces the timer (R12).
        reconnect_timer.cancel();

        if (auto status = security.validate(); !status) {
            complete_connect(fail(status.error()));
            return;
        }
        security.lock();
        user_disconnect = false;
        session_failure.reset();
        // A fresh session per connection round — the configuration
        // (credentials, KID, OTA policy) may have changed between
        // connections.
        session.reset();

        auto ssl = transport::make_ssl_context(*security.tls());
        if (!ssl) {
            complete_connect(fail(ssl.error()));
            return;
        }

        ensure_session();

        transport::WssOptions wss_options;
        wss_options.connect_timeout = options.connect_timeout;
        wss_options.idle_timeout = options.transport_idle_timeout;

        enter_state(ClientState::Resolving, std::nullopt, std::nullopt);

        dialer = transport::WssTransport::create(
            io.get_executor(), *ssl, endpoint, security.tls()->identity_policy(),
            wss_options);

        dialer->async_open([this, attempt = dialer](
                               Result<transport::TransportPtr> opened) {
            dialer.reset();
            if (!opened) {
                handle_connection_lost(opened.error());
                return;
            }
            transport = attempt;
            transport->set_receive_handler(
                [this](std::string_view frame) { session->on_text(frame); });
            transport->set_close_handler(
                [this](Error error) { handle_transport_closed(std::move(error)); });

            enter_state(ClientState::FountainHandshaking, std::nullopt, std::nullopt);
            session->on_transport_open();
            arm_tick();
        });
    }

    void ensure_session()
    {
        if (session) return;

        protocol::ControllerSessionConfig config;
        config.credentials = security.credentials();
        config.kid = security.kid().empty() ? "1" : security.kid();
        config.expected_device_id = security.expected_device_id();
        config.ota_policy = ota_policy;
        config.handshake_timeout = options.handshake_timeout;
        config.default_request_timeout = options.default_request_timeout;
        config.budget = options.rate_budget;

        session = std::make_unique<protocol::ControllerSession>(
            std::move(config),
            [this](std::string frame) {
                if (!transport || !transport->is_open()) return false;
                transport->async_send(std::move(frame), [](Status sent) {
                    if (!sent) {
                        log::debug(kLogCat, "send failed: " + sent.error().to_string());
                    }
                });
                return true;
            },
            [this](std::uint16_t code, std::string reason) {
                if (transport) transport->close(code, std::move(reason));
            });

        protocol::ControllerSession::Callbacks callbacks;
        callbacks.on_ready = [this](const ConnectionInfo& ready) {
            {
                std::lock_guard lock(info_mutex);
                info = ready;
            }
            was_ready = true;
            current_backoff = std::chrono::seconds{0};
            enter_state(ClientState::Ready, ready, std::nullopt);
            poller.on_reconnected();
            complete_connect(Result<ConnectionInfo>(ready));
        };
        callbacks.on_failed = [this](Error error) { session_failure = error; };
        callbacks.on_dp_report = [this](const nlohmann::json& dp,
                                        std::optional<std::uint32_t> seq) {
            datapoints.ingest_report(dp, DatapointSource::UnsolicitedReport, seq);
        };
        callbacks.on_heartbeat = [this](const Heartbeat& hb) { events.publish(hb); };
        callbacks.on_device_alert = [this](const DeviceAlert& alert) {
            events.publish(alert);
        };
        callbacks.on_ota_status = [this](const OtaStatus& status) {
            events.publish(status);
        };
        callbacks.on_error_report = [this](const ErrorReport& report) {
            events.publish(report);
        };
        callbacks.on_warning = [this](const ProtocolWarning& warning) {
            events.publish(warning);
        };
        callbacks.on_unknown = [this](std::string_view type,
                                      const nlohmann::json& message) {
            events.publish_unknown(type, message);
        };
        session->set_callbacks(std::move(callbacks));
    }

    void arm_tick()
    {
        tick_timer.expires_after(kTickInterval);
        tick_timer.async_wait([this](const boost::system::error_code& ec) {
            if (ec || !transport || !transport->is_open()) return;
            const auto now = protocol::Clock::now();
            session->tick(now);
            if (state.load() == ClientState::Ready) {
                poller.tick(now, session->last_tx());
            }
            arm_tick();
        });
    }

    // ------------------------------------------------------------------
    // Connection loss / reconnect (IO thread)
    // ------------------------------------------------------------------

    void handle_transport_closed(Error error)
    {
        tick_timer.cancel();
        transport.reset();

        // The session knows the more precise cause (auth_failed, ...).
        const Error cause = session_failure ? *session_failure : error;
        if (session) session->on_transport_closed(cause);
        datapoints.mark_all_stale();

        handle_connection_lost(cause);
    }

    void handle_connection_lost(Error cause)
    {
        if (user_disconnect) {
            security.unlock();
            enter_state(ClientState::Disconnected, std::nullopt, std::nullopt);
            complete_connect(fail(cancelled_error("connect")));
            complete_disconnect();
            return;
        }

        // A pending connect() call receives the error directly; the
        // automatic reconnect only kicks in AFTER a successful connect() —
        // first-connection errors are meant to fail visibly.
        complete_connect(fail(cause));

        if (!was_ready || !options.reconnect.enabled) {
            security.unlock();
            enter_state(ClientState::Disconnected, std::nullopt, std::move(cause));
            return;
        }

        const bool auth_failure = cause.domain == ErrorDomain::Authentication;
        std::chrono::seconds delay;
        if (auth_failure) {
            // Do not fill up the device's auth-failure strikes.
            delay = options.reconnect.authentication_delay;
        } else {
            current_backoff =
                current_backoff.count() == 0
                    ? options.reconnect.initial_delay
                    : std::min(options.reconnect.max_delay, current_backoff * 2);
            delay = current_backoff;
            if (options.reconnect.jitter && delay.count() > 1) {
                std::uniform_int_distribution<long> dist(0, delay.count() / 4);
                delay += std::chrono::seconds(dist(rng));
            }
        }

        log::info(kLogCat, "reconnect in " + std::to_string(delay.count()) +
                               " s (" + cause.to_string() + ")");
        enter_state(ClientState::Reconnecting, std::nullopt, std::move(cause));

        reconnect_timer.expires_after(delay);
        reconnect_timer.async_wait([this](const boost::system::error_code& ec) {
            if (ec || user_disconnect) return;
            begin_connect(nullptr);
        });
    }

    void complete_connect(Result<ConnectionInfo> result)
    {
        auto completions = std::exchange(connect_completions, {});
        for (auto& completion : completions) completion(result);
    }

    void complete_disconnect()
    {
        auto waiters = std::exchange(disconnect_waiters, {});
        for (auto& waiter : waiters) waiter();
    }

    // ------------------------------------------------------------------
    // Shutdown
    // ------------------------------------------------------------------

    // IO thread. done is invoked once the connection is REALLY closed (not
    // merely once the close has been initiated) — the basis for disconnect()
    // and async_disconnect() (review R6).
    void request_disconnect(std::function<void()> done)
    {
        user_disconnect = true;
        poller.stop();
        reconnect_timer.cancel();
        tick_timer.cancel();

        if (transport) {
            if (done) disconnect_waiters.push_back(std::move(done));
            transport->close(transport::kCloseNormal, "client shutdown");
            return;
        }
        if (dialer) {
            // Connection setup still in progress: cancel it; the open
            // completion lands in handle_connection_lost -> user_disconnect path.
            if (done) disconnect_waiters.push_back(std::move(done));
            dialer->cancel();
            return;
        }
        security.unlock();
        enter_state(ClientState::Disconnected, std::nullopt, std::nullopt);
        if (done) done();
    }

    void shutdown()
    {
        std::promise<void> done;
        asio::post(io, [this, &done] {
            request_disconnect([&done] { done.set_value(); });
        });
        done.get_future().wait();
    }
};

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(Endpoint endpoint) : impl_(std::make_unique<Impl>(std::move(endpoint)))
{
}

Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

Client::Builder Client::builder(Endpoint endpoint)
{
    return Builder{std::move(endpoint)};
}

SecurityConfiguration& Client::security() noexcept { return impl_->security; }
ClientOptions& Client::options() noexcept { return impl_->options; }

void Client::set_ota_policy(std::shared_ptr<protocol::OtaPolicy> policy)
{
    impl_->ota_policy = std::move(policy);
}

void Client::async_connect(ConnectCompletion completion)
{
    asio::post(impl_->io, [impl = impl_.get(), completion = std::move(completion)] {
        impl->begin_connect(std::move(completion));
    });
}

Result<ConnectionInfo> Client::connect()
{
    if (std::this_thread::get_id() == impl_->io_thread_id) {
        return fail(make_error(ErrorDomain::Internal, ErrorCode::InvalidState,
                               "connect() must not be called from the IO thread"));
    }

    struct Shared {
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<Result<ConnectionInfo>> result;
    };
    auto shared = std::make_shared<Shared>();

    async_connect([shared](Result<ConnectionInfo> value) {
        std::lock_guard lock(shared->mutex);
        shared->result = std::move(value);
        shared->cv.notify_all();
    });

    std::unique_lock lock(shared->mutex);
    shared->cv.wait(lock, [&] { return shared->result.has_value(); });
    return std::move(*shared->result);
}

Result<void> Client::disconnect()
{
    if (std::this_thread::get_id() == impl_->io_thread_id) {
        return fail(make_error(ErrorDomain::Internal, ErrorCode::InvalidState,
                               "disconnect() must not be called from the IO thread"));
    }
    impl_->shutdown();
    return ok();
}

void Client::async_disconnect(VoidCompletion completion)
{
    asio::post(impl_->io, [impl = impl_.get(), completion = std::move(completion)] {
        impl->request_disconnect(
            completion ? std::function<void()>([completion] { completion(ok()); })
                       : std::function<void()>{});
    });
}

ClientState Client::state() const noexcept { return impl_->state.load(); }
bool Client::ready() const noexcept { return state() == ClientState::Ready; }

std::optional<ConnectionInfo> Client::connection_info() const
{
    std::lock_guard lock(impl_->info_mutex);
    return impl_->info;
}

DatapointManager& Client::datapoints() noexcept { return impl_->datapoints; }
DatapointPoller& Client::polling() noexcept { return impl_->poller; }
CommandService& Client::commands() noexcept { return impl_->commands; }
LogService& Client::logs() noexcept { return impl_->logs; }
MaintenanceService& Client::maintenance() noexcept { return impl_->maintenance; }
EventBus& Client::events() noexcept { return impl_->events; }
RawProtocol& Client::raw() noexcept { return impl_->raw; }

// ---------------------------------------------------------------------------
// Client::Builder
// ---------------------------------------------------------------------------

Client::Builder& Client::Builder::with_tls(TlsCredentials credentials)
{
    tls_ = std::move(credentials);
    return *this;
}

Client::Builder& Client::Builder::with_hmac(HmacCredentials credentials)
{
    hmac_ = std::move(credentials);
    return *this;
}

Client::Builder& Client::Builder::with_credential_provider(
    std::shared_ptr<CredentialProvider> provider)
{
    provider_ = std::move(provider);
    return *this;
}

Client::Builder& Client::Builder::with_expected_device(std::string device_id)
{
    expected_device_ = std::move(device_id);
    return *this;
}

Client::Builder& Client::Builder::with_options(ClientOptions options)
{
    options_ = std::move(options);
    return *this;
}

Client::Builder& Client::Builder::with_reconnect(ReconnectPolicy policy)
{
    if (!options_) options_ = ClientOptions{};
    options_->reconnect = policy;
    return *this;
}

Client::Builder& Client::Builder::with_ota_policy(
    std::shared_ptr<protocol::OtaPolicy> policy)
{
    ota_ = std::move(policy);
    return *this;
}

Result<Client> Client::Builder::build()
{
    Client client{endpoint_};

    if (options_) client.options() = *options_;
    client.security().set_expected_device_id(expected_device_);
    if (ota_) client.set_ota_policy(std::move(ota_));

    if (tls_) {
        if (auto status = client.security().set_tls(std::move(*tls_)); !status) {
            return fail(status.error());
        }
    }
    if (hmac_) {
        if (auto status = client.security().set_hmac(std::move(*hmac_)); !status) {
            return fail(status.error());
        }
    }
    if (provider_) {
        if (auto status =
                client.security().set_credential_provider(std::move(provider_));
            !status) {
            return fail(status.error());
        }
    }

    // Report configuration errors early, not only at connect().
    if (auto status = client.security().validate(); !status) {
        return fail(status.error());
    }
    return client;
}

protocol::DispatcherMetrics Client::metrics() const
{
    // Hop onto the IO thread briefly — the dispatcher containers are not
    // synchronised for access from foreign threads.
    if (std::this_thread::get_id() == impl_->io_thread_id) {
        return impl_->session ? impl_->session->metrics()
                              : protocol::DispatcherMetrics{};
    }
    std::promise<protocol::DispatcherMetrics> promise;
    asio::post(impl_->io, [impl = impl_.get(), &promise] {
        promise.set_value(impl->session ? impl->session->metrics()
                                        : protocol::DispatcherMetrics{});
    });
    return promise.get_future().get();
}

}  // namespace fountainer
