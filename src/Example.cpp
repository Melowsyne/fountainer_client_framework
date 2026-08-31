// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Example.cpp — annotated tutorial example for the fountainer API.
//
// Shows, in ONE class (DeviceExample), the complete handling of a
// device:
//
//   1.  Connecting (builder, mTLS + HMAC, connect == Fountain RUNNING)
//   2.  Device information (ConnectionInfo)
//   3.  Reading datapoints — typed, several at once, dynamically
//   4.  Printing ALL 107 datapoints incl. catalog metadata
//   5.  Cache: cached() NEVER touches the network
//   6.  Subscriptions: typed change callbacks (RAII token)
//   7.  Polling: one scheduler batches cyclic reads
//   8.  Writing: single, as an atomic batch, with baseline restore
//   9.  Staging/commit: the configuration-editor workflow
//   10. Checking cross-field constraints BEFORE hitting the network
//   11. Commands (set_state, turn_on, ...)
//   12. Reading logs (incl. pagination)
//   13. Device events (alerts, heartbeat, connection state)
//   14. Raw protocol as an escape hatch + metrics
//   15. Disconnecting cleanly
//
// Build/run (Docker, config as for the CLI/hardware test):
//   docker compose run --rm client build
//   docker compose run --rm --entrypoint "" client
//       /app/build/example_class /app/config/client.fnt-000001.json
//
// All write accesses are chosen to be SAFE (DatapointRestoreGuard restores
// the original values); destructive commands (reboot, wd_fault) are only
// shown, not executed.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

#include <fountainer/client.hpp>                     // high-level facade
#include <fountainer/config.hpp>                     // JSON config (optional)
#include <fountainer/datapoints/constraints.hpp>     // cross-field rules
#include <fountainer/datapoints/generated.hpp>       // dp::... (107 points)
#include <fountainer/datapoints/restore_guard.hpp>   // baseline restore
#include <fountainer/logging/logger.hpp>

using namespace fountainer;
using namespace std::chrono_literals;

// ===========================================================================
// DeviceExample — encapsulates a device connection and demonstrates the API.
//
// In a real application you would hold the Client as a member exactly like
// this: ONE Client per device, long-lived, reconnects handled by the library.
// ===========================================================================
class DeviceExample {
public:
    // -----------------------------------------------------------------------
    // 1. CONNECTING
    //
    // The builder collects the configuration and validates it in build()
    // (missing/unreadable certificate files are caught HERE, not only at
    // connect time). The return value is Result<Client> — errors are values,
    // not exceptions.
    // -----------------------------------------------------------------------
    static Result<DeviceExample> create(const Config& config)
    {
        // Load the HMAC key (64 hex characters) from the file. The path is
        // in the config — the key itself NEVER ends up in logs.
        auto hmac = HmacCredentials::from_file(config.fountain.kid,
                                               config.fountain.hmac_key_file);
        if (!hmac) return fail(hmac.error());

        auto built =
            Client::builder(Endpoint{config.device.host, config.device.port,
                                     config.device.path,
                                     config.device.subprotocol})
                // mTLS: private CA + our own client certificate. The device
                // has a DHCP IP without a DNS name, so EXPLICITLY only the
                // certificate chain is verified (no silent "insecure").
                .with_tls(TlsCredentials::mutual_tls(
                    config.tls.ca_file, config.tls.client_cert_file,
                    config.tls.client_key_file,
                    EndpointIdentityPolicy::VerifyCertificateChainOnly))
                // Fountain HMAC: authenticates control messages INSIDE the
                // session (second security layer in addition to TLS).
                .with_hmac(std::move(*hmac))
                // Accept only this device — a hello from a foreign device is
                // rejected with 4004.
                .with_expected_device(config.fountain.device_id)
                // Reconnecting is handled by the library (backoff 1..60 s,
                // auth errors deliberately get a 300 s spacing).
                .with_reconnect({.enabled = true,
                                 .initial_delay = 1s,
                                 .max_delay = 60s})
                .build();
        if (!built) return fail(built.error());
        return DeviceExample(std::move(*built));
    }

    // Runs all example sections one after another.
    int run()
    {
        if (!connect()) return 1;
        show_connection_info();
        read_single_and_multi();
        read_dynamic();
        list_all_datapoints();
        cache_example();
        subscription_and_polling_example();
        write_examples();
        staging_commit_example();
        command_examples();
        log_examples();
        raw_and_metrics_example();
        disconnect();
        return 0;
    }

private:
    explicit DeviceExample(Client client) : client_(std::move(client))
    {
        // ------------------------------------------------------------------
        // 13. DEVICE EVENTS — best registered BEFORE connect.
        //
        // Spontaneous messages from the device are domain objects, not log
        // lines. The subscription tokens are RAII: destructor = unsubscribe.
        // Callbacks run on the library's IO thread -> keep them short and
        // make NO blocking API calls inside them (that is what the async_*
        // variants are for).
        // ------------------------------------------------------------------
        event_subscriptions_.push_back(client_.events().on_connection_state(
            [](const ConnectionStateChange& change) {
                std::cout << "[event] State: " << to_string(change.previous)
                          << " -> " << to_string(change.current);
                if (change.cause) std::cout << " (" << change.cause->to_string() << ")";
                std::cout << '\n';
            }));

        event_subscriptions_.push_back(client_.events().on_device_alert(
            [](const DeviceAlert& alert) {
                // E.g. pressure alarm: code="PRESSURE_HIGH", datapoint, value...
                std::cout << "[event] ALARM " << alert.code << ": " << alert.detail
                          << '\n';
            }));

        event_subscriptions_.push_back(client_.events().on_heartbeat(
            [](const Heartbeat& heartbeat) {
                // Disabled locally (the firmware sends no heartbeat); in
                // backend operation it arrives cyclically.
                std::cout << "[event] Heartbeat, uptime=" << heartbeat.uptime_s
                          << " s\n";
            }));

        event_subscriptions_.push_back(client_.events().on_protocol_warning(
            [](const ProtocolWarning& warning) {
                // Non-fatal anomalies (discarded frames, ...).
                std::cout << "[event] Protocol notice: " << warning.reason << '\n';
            }));
    }

    // -----------------------------------------------------------------------
    bool connect()
    {
        std::cout << "\n=== 1. Connecting ===\n";

        // connect() only succeeds once the FOUNTAIN SESSION is running
        // (hello -> hello_ack -> signed proof -> RUNNING). A mere
        // TCP/TLS/WebSocket success is not enough — before that the device
        // would ignore all control messages.
        auto info = client_.connect();
        if (!info) {
            // Errors are typed: domain (tls/timeout/auth/...) + code.
            std::cerr << "Connection failed: " << info.error().to_string()
                      << '\n';
            return false;
        }
        info_ = *info;
        return true;
    }

    // -----------------------------------------------------------------------
    void show_connection_info()
    {
        std::cout << "\n=== 2. Device information (from the handshake) ===\n";
        // ConnectionInfo comes from the device's hello plus the result of
        // the authentication negotiation.
        std::cout << "  device_id : " << info_.device_id << '\n'
                  << "  serial    : " << info_.serial << '\n'
                  << "  firmware  : " << info_.firmware_version << '\n'
                  << "  hardware  : " << info_.hardware_revision << '\n'
                  << "  boot      : " << info_.boot_reason << '\n'
                  << "  protocol  : v" << int(info_.protocol_version) << '\n'
                  << "  auth      : " << info_.auth_scheme
                  << " (kid=" << info_.auth_kid << ")\n";
    }

    // -----------------------------------------------------------------------
    void read_single_and_multi()
    {
        std::cout << "\n=== 3. Typed reads ===\n";

        // SINGLE VALUE: dp::Fon_Current_Pressure is a GENERATED constant
        // from the firmware source dp_list.def. The return type is
        // Result<float> — the C++ type is carried by the datapoint, no JSON.
        auto pressure = client_.datapoints().read(dp::Fon_Current_Pressure);
        if (pressure) {
            std::cout << "  Pressure: " << *pressure << " bar\n";
        } else {
            std::cout << "  Pressure read failed: "
                      << pressure.error().to_string() << '\n';
        }

        // SEVERAL VALUES IN ONE REQUEST: the names are bundled into a
        // single dp_read (important because of the firmware's rate limit).
        auto snapshot = client_.datapoints().read(
            dp::Fon_Current_State, dp::Fon_Relay_Output, dp::System_Uptime,
            dp::System_RSSI);
        if (snapshot) {
            // Typed access to the snapshot:
            auto state = snapshot->get(dp::Fon_Current_State);    // uint8_t
            auto relay = snapshot->get(dp::Fon_Relay_Output);     // bool
            auto uptime = snapshot->get(dp::System_Uptime);       // uint32_t
            auto rssi = snapshot->get(dp::System_RSSI);           // int8_t
            if (state && relay && uptime && rssi) {
                std::cout << "  State=" << int(*state)
                          << " Relay=" << (*relay ? "ON" : "off")
                          << " Uptime=" << *uptime << " s"
                          << " RSSI=" << int(*rssi) << " dBm\n";
            }
        }

        // COMPILE-TIME PROTECTION: this would NOT compile, because
        // Device_SW_Version is ReadOnly (WritableDatapoint concept):
        //   client_.datapoints().write(dp::Device_SW_Version, "fake");
    }

    // -----------------------------------------------------------------------
    void read_dynamic()
    {
        std::cout << "\n=== 3b. Dynamic reads (runtime name) ===\n";

        // For tools/GUIs that only know names at runtime: the return value
        // is a DatapointValue (std::variant over all wire types).
        auto value = client_.datapoints().read("Ambient_Temperature");
        if (value) {
            const auto* descriptor = catalog::find("Ambient_Temperature");
            // to_display_string uses the catalog metadata (unit, decimal
            // places) from the dp_list.def annotations.
            std::cout << "  Ambient_Temperature = "
                      << to_display_string(*descriptor, *value) << '\n';
        }

        // Unknown names are a clean error, not a crash:
        auto unknown = client_.datapoints().read("Does_Not_Exist");
        std::cout << "  Unknown name -> "
                  << (unknown ? "?" : std::string(to_string(unknown.error().code)))
                  << '\n';
    }

    // -----------------------------------------------------------------------
    void list_all_datapoints()
    {
        std::cout << "\n=== 4. ALL datapoints (catalog + live values) ===\n";

        // read_all() ALWAYS fetches all names explicitly — for an empty
        // list the local server would deliver only the VOLATILE points
        // (a firmware quirk that the API encapsulates away).
        auto all = client_.datapoints().read_all();
        if (!all) {
            std::cout << "  read_all failed: " << all.error().to_string()
                      << '\n';
            return;
        }

        // The GENERATED catalog (catalog::all()) provides type, access,
        // persistence, value range and UI metadata for every point — the
        // single source of truth is the firmware's dp_list.def.
        std::printf("  %-26s %-4s %-2s %-8s %-12s %s\n", "Name", "Type", "RW",
                    "Persist", "Range", "Value");
        for (const auto& descriptor : catalog::all()) {
            const DatapointValue* value = all->find(descriptor.name);

            char range[32] = "-";
            if (descriptor.numeric() && !std::isnan(descriptor.min)) {
                std::snprintf(range, sizeof range, "%g..%g", descriptor.min,
                              descriptor.max);
            }

            // Do not print SENSITIVE values (passwords). The catalog marks
            // them with PollClass::Disabled — the same policy that also
            // prevents the poller from fetching them automatically.
            std::string display = "(missing)";
            if (value != nullptr) {
                const bool sensitive = descriptor.poll_class == PollClass::Disabled &&
                                       descriptor.type == DatapointType::Str;
                display = sensitive ? "••• (sensitive, not shown)"
                                    : to_display_string(descriptor, *value);
            }

            std::printf("  %-26.*s %-4.*s %-2.*s %-8.*s %-12s %s\n",
                        int(descriptor.name.size()), descriptor.name.data(),
                        int(to_string(descriptor.type).size()),
                        to_string(descriptor.type).data(),
                        int(to_string(descriptor.access).size()),
                        to_string(descriptor.access).data(),
                        int(to_string(descriptor.persistence).size()),
                        to_string(descriptor.persistence).data(), range,
                        display.c_str());
        }
        std::cout << "  -> " << all->size() << " of " << kDatapointCount
                  << " datapoints read\n";
    }

    // -----------------------------------------------------------------------
    void cache_example()
    {
        std::cout << "\n=== 5. Cache (never the network) ===\n";

        // TERMINOLOGY DISCIPLINE of the API:  read() = network,  cached() =
        // ONLY the local cache (filled by reads, polling, push reports and
        // write readbacks). cached() is always immediate and can never fail
        // — at most "no value available yet".
        if (auto cached = client_.datapoints().cached(dp::Fon_Current_Pressure)) {
            std::cout << "  Pressure from the cache: " << cached->value << " bar"
                      << "  (source: " << to_string(cached->source)
                      << ", quality: " << to_string(cached->quality) << ")\n";
            // quality automatically becomes "stale" on connection loss —
            // a GUI can keep showing the last value AND mark it as
            // outdated.
        }
    }

    // -----------------------------------------------------------------------
    void subscription_and_polling_example()
    {
        std::cout << "\n=== 6./7. Subscriptions + Polling (6 s) ===\n";

        // SUBSCRIPTION: typed callback on a VALUE CHANGE in the cache.
        // The token is RAII — once it goes out of scope, you are unsubscribed.
        auto pressure_subscription = client_.datapoints().subscribe(
            dp::Fon_Current_Pressure, [](const DatapointChange<float>& change) {
                std::cout << "  [change] Pressure: ";
                if (change.old_value) std::cout << *change.old_value << " -> ";
                std::cout << change.value << " bar\n";
            });

        auto uptime_subscription = client_.datapoints().subscribe(
            dp::System_Uptime, [](const DatapointChange<std::uint32_t>& change) {
                std::cout << "  [change] Uptime: " << change.value << " s ("
                          << to_string(change.source) << ")\n";
            });

        // POLLING: ONE scheduler for all cyclic reads. Due points are
        // BUNDLED per cycle into a few dp_read requests and sent with low
        // priority — user actions (writes, commands) always overtake the
        // polling queue.
        auto& poll = client_.polling();
        poll.every(500ms, dp::Fon_Current_Pressure, dp::Fon_Current_State);
        poll.every(5s, dp::System_Uptime, dp::System_RSSI);
        poll.once(dp::Device_Serial_Number);   // once per connection
        // Alternatively: poll.start_defaults() activates the poll classes
        // stored in the catalog (Realtime 1 s / Status 5 s / Config 60 s).
        poll.start();

        std::this_thread::sleep_for(6s);
        poll.stop();

        const auto stats = poll.stats();
        std::cout << "  Poll statistics: " << stats.requests << " Requests, "
                  << stats.coalesced_points << " coalesced points, "
                  << stats.skipped_inflight << " skipped (in-flight)\n";
        // The subscriptions end here automatically (RAII).
    }

    // -----------------------------------------------------------------------
    void write_examples()
    {
        std::cout << "\n=== 8. Writing (with baseline restore) ===\n";

        // SAVE THE BASELINE: the RestoreGuard reads the current values and
        // restores them when the scope is left — even if something goes
        // wrong in between. Mandatory for tests/maintenance.
        auto guard = DatapointRestoreGuard::capture(
            client_.datapoints(),
            {"Fon_Event_Label", "Fon_Report_Interval", "Fon_Min_On_Time"});
        if (!guard) {
            std::cout << "  Baseline failed: " << guard.error().to_string()
                      << '\n';
            return;
        }

        // SINGLE TYPED WRITE: the value range (here 0..7) is already checked
        // CLIENT-SIDE; the firmware validates again anyway (it remains
        // authoritative) and returns a READBACK, which is automatically
        // taken over into the cache.
        auto write = client_.datapoints().write(dp::Fon_Event_Label,
                                                std::uint8_t{5});
        if (!write) {
            // TRANSPORT error (timeout, connection lost, ...).
            std::cout << "  Write error: " << write.error().to_string() << '\n';
        } else if (!write->applied()) {
            // REMOTE REJECTION: transport ok, the DEVICE has rejected it.
            // This is a normal result, NOT an error path!
            for (const auto& error : write->errors) {
                std::cout << "  rejected: " << error.datapoint << " ("
                          << error.reason << ")\n";
            }
        } else if (const auto* echoed = write->readback.find("Fon_Event_Label")) {
            std::cout << "  Fon_Event_Label=5 written, readback: "
                      << to_display_string(catalog::at(dp::Fon_Event_Label),
                                           *echoed)
                      << '\n';
        }

        // ATOMIC BATCH: either ALL points are applied or none of them
        // (dp_write_batch semantics of the firmware).
        DatapointWriteSet batch;
        batch.set(dp::Fon_Report_Interval, std::uint16_t{15});
        batch.set(dp::Fon_Min_On_Time, std::uint16_t{25});
        if (auto batch_result = client_.datapoints().write(batch);
            batch_result && batch_result->applied()) {
            std::cout << "  Batch (2 points) written atomically\n";
            if (batch_result->warning) {
                // E.g. "nvs_save_failed": the value is active in RAM but NOT
                // stored persistently — important for config tools!
                std::cout << "  WARNING from the device: " << *batch_result->warning
                          << '\n';
            }
        }

        // DELIBERATELY INVALID: out-of-range is caught BEFORE the network
        // (fast feedback for UIs).
        auto invalid = client_.datapoints().write(dp::Fon_Event_Label,
                                                  std::uint8_t{99});
        std::cout << "  Write 99 (max 7) -> "
                  << (invalid ? "?!" : std::string(to_string(invalid.error().code)))
                  << " (blocked client-side)\n";

        // RESTORE THE BASELINE: explicitly, with result checking (otherwise
        // the destructor would do it best-effort).
        if (auto restored = guard->restore(); restored && restored->applied()) {
            std::cout << "  Baseline restored\n";
        }
    }

    // -----------------------------------------------------------------------
    void staging_commit_example()
    {
        std::cout << "\n=== 9./10. Staging/commit + cross-field constraints ===\n";

        auto& dps = client_.datapoints();

        // The EDITOR WORKFLOW: UI inputs are STAGED locally (no network
        // traffic), validated together and then committed ATOMICALLY.
        auto guard = DatapointRestoreGuard::capture(
            dps, {"Fon_Alert_Low_Pressure", "Fon_Min_Pressure",
                  "Fon_Max_Pressure", "Fon_Alert_High_Pressure"});
        if (!guard) return;

        // The pressure chain is cross-field coupled:
        //   AlertLow < Min < Max < AlertHigh   (firmware: dp_constraints_ok)
        (void)dps.stage(dp::Fon_Alert_Low_Pressure, 0.3f);
        (void)dps.stage(dp::Fon_Min_Pressure, 2.0f);
        (void)dps.stage(dp::Fon_Max_Pressure, 3.5f);
        (void)dps.stage(dp::Fon_Alert_High_Pressure, 4.5f);

        // validate_staged() checks individual limits AND the cross-field
        // rules (missing operands come from the cache) — the same rules the
        // firmware applies, just already BEFORE the network:
        if (auto valid = dps.validate_staged(); !valid) {
            std::cout << "  Validation: " << valid.error().to_string() << '\n';
            dps.discard_staged();
            return;
        }
        std::cout << "  Staged values validated (incl. chain rules)\n";

        // Example of a DETECTED violation, without asking the device:
        DatapointWriteSet broken;
        broken.set(dp::Fon_Min_Pressure, 3.0f);
        broken.set(dp::Fon_Max_Pressure, 2.0f);   // Max < Min!
        auto violation = dps.validate_constraints(broken);
        std::cout << "  Broken chain -> "
                  << (violation ? "?!" : violation.error().message) << '\n';

        // COMMIT: all staged values in ONE atomic dp_write.
        if (auto committed = dps.commit(); committed && committed->applied()) {
            std::cout << "  Commit applied (on 'rejected' the staging "
                         "would be kept for corrections)\n";
        }

        (void)guard->restore();   // restore original values
    }

    // -----------------------------------------------------------------------
    void command_examples()
    {
        std::cout << "\n=== 11. Commands ===\n";

        // Commands are TYPED — no wire strings in application code.
        // set_state(Off) internally runs with the highest priority
        // (SafetyControl): an emergency stop never waits behind polling.
        auto result = client_.commands().set_state(FountainState::Auto);
        if (!result) {
            std::cout << "  Transport error: " << result.error().to_string() << '\n';
        } else if (result->applied()) {
            std::cout << "  set_state(Auto) -> applied\n";
        } else {
            // Rejection by the DEVICE LOGIC (e.g. a latched fault) —
            // again: a normal result, not a transport error.
            std::cout << "  set_state(Auto) -> rejected ("
                      << result->error.value_or("?") << ")\n";
        }

        // Further commands (only shown here, not executed):
        //   client_.commands().set_state(FountainState::Off);   // emergency stop
        //   client_.commands().turn_on_for(90s);   // rounds to 30 s steps
        //   client_.commands().restart_pump();
        //   client_.commands().reboot();           // device reboot!
        //
        // Diagnostics (deliberately hidden behind maintenance() AND an
        // options switch, design concept §15.2):
        //   client_.options().enable_test_commands = true;
        //   client_.maintenance().diagnostics().force_poor_link(60s);
        //   client_.maintenance().diagnostics().inject_watchdog_fault(5s);
    }

    // -----------------------------------------------------------------------
    void log_examples()
    {
        std::cout << "\n=== 12. Device logs ===\n";

        // ONE page with filter: since sequence X, minimum level, max. count.
        auto batch = client_.logs().read({.since_sequence = 0,
                                          .minimum_level = LogLevel::Info,
                                          .max_records = 5});
        if (batch) {
            std::cout << "  boot_id=" << batch->boot_id
                      << " next_seq=" << batch->next_seq << '\n';
            for (const auto& record : batch->records) {
                std::cout << "    #" << record.sequence << " [lvl "
                          << int(record.level) << "] " << record.text << '\n';
            }
        }

        // COMPLETE runtime log: read_all() paginates on its own and resumes
        // from the LAST RECEIVED record (the firmware's byte budget can
        // truncate batches — next_seq alone is not enough).
        if (auto everything = client_.logs().read_all()) {
            std::cout << "  read_all(): " << everything->size()
                      << " records in total\n";
        }

        // Logs of the PREVIOUS boot (e.g. after a crash):
        if (auto previous = client_.logs().read_previous({.max_records = 3})) {
            std::cout << "  Previous boot available: "
                      << (previous->previous_boot_available ? "yes" : "no")
                      << '\n';
            // Acknowledge only once the data has been saved (DESTRUCTIVE):
            //   client_.logs().ack_previous(previous->boot_id);
        }

        // Further operations: flush() (Log_Command=3, harmless) and
        // clear_runtime() (Log_Command=1, DESTRUCTIVE — erases the log).
    }

    // -----------------------------------------------------------------------
    void raw_and_metrics_example()
    {
        std::cout << "\n=== 14. Raw protocol + metrics ===\n";

        // ESCAPE HATCH for tools/diagnostics: raw requests with their own
        // deadline. NOT the right way for normal applications — that is
        // what the typed services above are for.
        protocol::RequestSpec spec;
        spec.type = "dp_read";
        spec.body = {{"names", {"System_Memory_Free"}}};
        spec.timeout = 5000ms;
        if (auto response = client_.raw().request(std::move(spec))) {
            std::cout << "  raw dp_read -> "
                      << (*response)["dp"]["System_Memory_Free"] << " B free\n";
        }

        // Dispatcher metrics for observability/debugging:
        const auto metrics = client_.metrics();
        std::cout << "  Metrics: sent=" << metrics.sent
                  << " answered=" << metrics.completed
                  << " timeouts=" << metrics.timeouts
                  << " rate-limit deferrals=" << metrics.rate_limit_deferrals
                  << '\n';
    }

    // -----------------------------------------------------------------------
    void disconnect()
    {
        std::cout << "\n=== 15. Disconnecting ===\n";
        // disconnect() only returns once the connection is REALLY closed
        // (close handshake completed). Open requests fail in a typed way
        // with ErrorCode::Disconnected — never with null data.
        (void)client_.disconnect();
        std::cout << "  disconnected, state: " << to_string(client_.state()) << '\n';
    }

    Client client_;
    ConnectionInfo info_{};
    std::vector<Subscription> event_subscriptions_;
};

// ===========================================================================
// main — load the configuration and run the example.
// ===========================================================================
int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <config.json>\n"
                  << "  e.g. /app/config/client.fnt-000001.json\n";
        return 2;
    }

    // The JSON config bundles endpoint + certificate paths + device identity.
    // An application can just as well set all of this directly in code (see
    // DeviceExample::create) — the file is merely a convenience.
    Config config;
    try {
        config = load_config(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "config: " << e.what() << '\n';
        return 2;
    }
    log::set_level(log::Level::Warn);   // example output should dominate

    auto example = DeviceExample::create(config);
    if (!example) {
        std::cerr << "Setup failed: " << example.error().to_string() << '\n';
        return 1;
    }
    return example->run();
}
