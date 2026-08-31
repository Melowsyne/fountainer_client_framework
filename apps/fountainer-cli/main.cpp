// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// fountainer-cli — command-line client ON TOP OF the framework API (design
// concept §25: tools use the same public API as later applications; the old
// "Application + sequence" special case goes away).
//
//   fountainer-cli <config.json> info
//   fountainer-cli <config.json> read <name> [...]
//   fountainer-cli <config.json> read-all
//   fountainer-cli <config.json> write <name>=<value> [...]
//   fountainer-cli <config.json> watch [interval_ms] [name ...]
//   fountainer-cli <config.json> logs [prev|all]
//   fountainer-cli <config.json> command <set_state On|Off|Auto|Manual |
//                                         turn_on <seconds> | restart | reboot>
//   fountainer-cli <config.json> script     # fountain.sequence from the config
//
// The JSON config is the same as before (config/client*.json) — the
// Docker campaigns keep working.
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include <fountainer/client.hpp>
#include <fountainer/config.hpp>
#include <fountainer/datapoints/generated.hpp>
#include <fountainer/logging/logger.hpp>
#include <fountainer/protocol/fountain_messages.hpp>

using namespace fountainer;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_stop{false};

void handle_signal(int) { g_stop = true; }

bool is_ip_literal(const std::string& host)
{
    return host.find_first_not_of("0123456789.") == std::string::npos ||
           host.find(':') != std::string::npos;
}

// Existing config file -> framework objects.
Result<void> configure(Client& client, const Config& config)
{
    EndpointIdentityPolicy policy = EndpointIdentityPolicy::VerifyDnsName;
    if (config.tls.verify_hostname == "off" ||
        (config.tls.verify_hostname == "auto" && is_ip_literal(config.device.host))) {
        // DHCP device IP without an IP SAN: verify the chain against the private CA.
        policy = EndpointIdentityPolicy::VerifyCertificateChainOnly;
    }

    auto tls = TlsCredentials::mutual_tls(config.tls.ca_file,
                                          config.tls.client_cert_file,
                                          config.tls.client_key_file, policy);
    if (!config.tls.verify_peer) {
        tls.set_unsafe_options(UnsafeTlsOptions{.disable_peer_verification = true});
    }
    if (auto status = client.security().set_tls(std::move(tls)); !status) {
        return status;
    }

    client.security().set_expected_device_id(config.fountain.device_id);
    auto hmac = HmacCredentials::from_file(config.fountain.kid,
                                           config.fountain.hmac_key_file);
    if (!hmac) return fail(hmac.error());
    if (auto status = client.security().set_hmac(std::move(*hmac)); !status) {
        return status;
    }

    client.options().connect_timeout = config.connection.connect_timeout;
    client.options().transport_idle_timeout = config.connection.idle_timeout;
    client.options().handshake_timeout = config.fountain.handshake_timeout;
    client.options().reconnect.enabled = config.connection.auto_reconnect;
    client.options().reconnect.initial_delay = config.connection.reconnect_initial;
    client.options().reconnect.max_delay = config.connection.reconnect_max;
    return ok();
}

int print_error(const Error& error)
{
    std::cerr << "error: " << error.to_string() << '\n';
    return 1;
}

void print_value(const DatapointDescriptor& descriptor, const DatapointValue& value)
{
    std::cout << descriptor.name << " = " << to_display_string(descriptor, value)
              << '\n';
}

void print_snapshot(const DatapointSnapshot& snapshot)
{
    for (const auto& [name, value] : snapshot.values()) {
        print_value(*catalog::find(name), value);
    }
    for (const auto& name : snapshot.unknown()) {
        std::cout << name << " = ? (unknown)\n";
    }
}

// "name=value" -> WriteSet entry with catalog-based type conversion.
Status parse_assignment(DatapointWriteSet& changes, const std::string& assignment)
{
    const auto eq = assignment.find('=');
    if (eq == std::string::npos) {
        return fail(validation_error(ErrorCode::ConstraintViolation,
                                     "expected <name>=<value>: " + assignment));
    }
    const std::string name = assignment.substr(0, eq);
    const std::string text = assignment.substr(eq + 1);

    const DatapointDescriptor* descriptor = catalog::find(name);
    if (descriptor == nullptr) {
        return fail(validation_error(ErrorCode::UnknownDatapoint,
                                     "unknown datapoint '" + name + "'", name));
    }

    nlohmann::json raw;
    switch (descriptor->type) {
    case DatapointType::Bool:
        raw = (text == "1" || text == "true" || text == "on");
        break;
    case DatapointType::Str:
    case DatapointType::U64:
        raw = text;
        break;
    default:
        try {
            raw = std::stod(text);
        } catch (const std::exception&) {
            return fail(validation_error(ErrorCode::TypeMismatch,
                                         name + ": '" + text + "' is not a number",
                                         name));
        }
    }

    auto value = value_from_json(*descriptor, raw);
    if (!value) return fail(value.error());
    return changes.set(name, std::move(*value));
}

int run_watch(Client& client, std::vector<std::string> names,
              std::chrono::milliseconds interval)
{
    auto subscription = client.datapoints().subscribe_all(
        [](const AnyDatapointChange& change) {
            // endl: immediately visible with piped output (Docker)
            std::cout << change.descriptor->name << " = "
                      << to_display_string(*change.descriptor, change.value)
                      << "   [" << to_string(change.source) << "]" << std::endl;
        });

    if (names.empty()) {
        client.polling().start_defaults();
    } else {
        if (auto status = client.polling().every(interval, names); !status) {
            return print_error(status.error());
        }
        client.polling().start();
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    while (!g_stop) {
        std::this_thread::sleep_for(200ms);
    }

    const auto stats = client.polling().stats();
    std::cerr << "\npoll stats: requests=" << stats.requests
              << " coalesced=" << stats.coalesced_points
              << " skipped=" << stats.skipped_inflight
              << " missed=" << stats.missed_deadlines
              << " keepalives=" << stats.keepalives
              << " failures=" << stats.failures << '\n';
    return 0;
}

int run_logs(Client& client, const std::string& mode)
{
    if (mode == "prev") {
        auto batch = client.logs().read_previous({});
        if (!batch) return print_error(batch.error());
        std::cout << "boot_id=" << batch->boot_id
                  << " available=" << (batch->previous_boot_available ? "yes" : "no")
                  << " records=" << batch->records.size() << '\n';
        for (const auto& record : batch->records) {
            std::cout << record.sequence << " [" << int(record.level) << "] "
                      << record.text << '\n';
        }
        return 0;
    }

    Result<std::vector<LogRecord>> records =
        mode == "all" ? client.logs().read_all()
                      : [&] {
                            auto batch = client.logs().read({});
                            if (!batch)
                                return Result<std::vector<LogRecord>>{
                                    unexpected_t{batch.error()}};
                            return Result<std::vector<LogRecord>>{
                                std::move(batch->records)};
                        }();
    if (!records) return print_error(records.error());
    for (const auto& record : *records) {
        std::cout << record.sequence << " [" << int(record.level) << "] "
                  << record.text << '\n';
    }
    std::cerr << records->size() << " records\n";
    return 0;
}

int run_command(Client& client, const std::vector<std::string>& args)
{
    if (args.empty()) {
        std::cerr << "usage: command <set_state <On|Off|Auto|Manual> | "
                     "turn_on <seconds> | restart | reboot>\n";
        return 2;
    }

    Result<CommandResult> result{unexpected_t{internal_error("unhandled")}};
    if (args[0] == "set_state" && args.size() == 2) {
        FountainState state;
        if (args[1] == "On") state = FountainState::On;
        else if (args[1] == "Off") state = FountainState::Off;
        else if (args[1] == "Auto") state = FountainState::Auto;
        else if (args[1] == "Manual") state = FountainState::Manual;
        else {
            std::cerr << "unknown state '" << args[1] << "'\n";
            return 2;
        }
        result = client.commands().set_state(state);
    } else if (args[0] == "turn_on" && args.size() == 2) {
        result = client.commands().turn_on_for(std::chrono::seconds(std::stol(args[1])));
    } else if (args[0] == "restart") {
        result = client.commands().restart_pump();
    } else if (args[0] == "reboot") {
        result = client.commands().reboot();
    } else {
        std::cerr << "unknown command '" << args[0] << "'\n";
        return 2;
    }

    if (!result) return print_error(result.error());
    std::cout << result->command << ": "
              << (result->applied() ? "applied" : "rejected");
    if (result->error) std::cout << " (" << *result->error << ")";
    std::cout << '\n';
    return result->applied() ? 0 : 3;
}

// Compatibility: play back the config's fountain.sequence serially (formerly
// the core of the Application, now a thin tool on top of raw()).
int run_script(Client& client, const Config& config)
{
    do {
        for (const auto& step : config.fountain.sequence) {
            if (g_stop) return 0;
            if (step.delay.count() > 0) std::this_thread::sleep_for(step.delay);

            protocol::RequestSpec spec;
            spec.type = step.type;
            spec.body = step.body;
            spec.timeout = step.timeout;

            const auto* meta = protocol::find_meta(step.type);
            if (meta == nullptr) {
                std::cerr << "unknown type '" << step.type << "'\n";
                return 2;
            }
            if (!meta->request) {
                // Fire-and-forget: there is no defined response.
                client.raw().send(step.type, step.body);
                continue;
            }
            auto response = client.raw().request(std::move(spec));
            if (!response) {
                std::cerr << step.type << " -> " << response.error().to_string()
                          << '\n';
                continue;
            }
            std::cout << step.type << " -> " << response->dump() << '\n';
        }
    } while (config.fountain.loop && !g_stop);
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " <config.json> <info|read|read-all|write|watch|logs|"
                     "command|script> [args...]\n";
        return 2;
    }

    Config config;
    try {
        config = load_config(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "config: " << e.what() << '\n';
        return 1;
    }
    log::set_level(config.logging.level);

    Client client{Endpoint{config.device.host, config.device.port,
                           config.device.path, config.device.subprotocol}};
    if (auto status = configure(client, config); !status) {
        return print_error(status.error());
    }

    auto connection = client.connect();
    if (!connection) return print_error(connection.error());
    std::cerr << "connected: device=" << connection->device_id
              << " serial=" << connection->serial
              << " fw=" << connection->firmware_version << '\n';

    const std::string verb = argv[2];
    std::vector<std::string> args(argv + 3, argv + argc);
    int exit_code = 0;

    if (verb == "info") {
        std::cout << "device_id: " << connection->device_id << '\n'
                  << "serial:    " << connection->serial << '\n'
                  << "firmware:  " << connection->firmware_version << '\n'
                  << "hw_rev:    " << connection->hardware_revision << '\n'
                  << "protocol:  v" << int(connection->protocol_version) << '\n'
                  << "auth:      " << connection->auth_scheme << " kid="
                  << connection->auth_kid << '\n';
    } else if (verb == "read" && !args.empty()) {
        auto snapshot = client.datapoints().read_names(args);
        if (!snapshot) exit_code = print_error(snapshot.error());
        else print_snapshot(*snapshot);
    } else if (verb == "read-all") {
        auto snapshot = client.datapoints().read_all();
        if (!snapshot) exit_code = print_error(snapshot.error());
        else {
            print_snapshot(*snapshot);
            std::cerr << snapshot->size() << " datapoints\n";
        }
    } else if (verb == "write" && !args.empty()) {
        DatapointWriteSet changes;
        for (const auto& assignment : args) {
            if (auto status = parse_assignment(changes, assignment); !status) {
                return print_error(status.error());
            }
        }
        auto result = client.datapoints().write(changes);
        if (!result) exit_code = print_error(result.error());
        else if (!result->applied()) {
            std::cerr << "rejected:\n";
            for (const auto& error : result->errors) {
                std::cerr << "  " << error.datapoint << ": " << error.reason << '\n';
            }
            exit_code = 3;
        } else {
            std::cout << "applied";
            if (result->warning) std::cout << " (warning: " << *result->warning << ")";
            std::cout << "\nreadback:\n";
            print_snapshot(result->readback);
        }
    } else if (verb == "watch") {
        std::chrono::milliseconds interval{1000};
        std::vector<std::string> names = args;
        if (!names.empty() && names.front().find_first_not_of("0123456789") ==
                                  std::string::npos) {
            interval = std::chrono::milliseconds(std::stol(names.front()));
            names.erase(names.begin());
        }
        exit_code = run_watch(client, std::move(names), interval);
    } else if (verb == "logs") {
        exit_code = run_logs(client, args.empty() ? "" : args[0]);
    } else if (verb == "command") {
        exit_code = run_command(client, args);
    } else if (verb == "raw" && !args.empty()) {
        // Escape hatch for diagnostics: fountainer-cli <cfg> raw <type> [json]
        protocol::RequestSpec spec;
        spec.type = args[0];
        if (args.size() > 1) {
            spec.body = nlohmann::json::parse(args[1], nullptr, false);
            if (spec.body.is_discarded()) {
                std::cerr << "invalid JSON body\n";
                return 2;
            }
        }
        const auto* meta = protocol::find_meta(spec.type);
        if (meta == nullptr) {
            std::cerr << "unknown message type '" << spec.type << "'\n";
            return 2;
        }
        if (!meta->request) {
            client.raw().send(spec.type, spec.body);
            std::cout << "sent (fire-and-forget)\n";
        } else {
            auto response = client.raw().request(std::move(spec));
            if (!response) exit_code = print_error(response.error());
            else std::cout << response->dump(2) << '\n';
        }
    } else if (verb == "script") {
        std::signal(SIGINT, handle_signal);
        exit_code = run_script(client, config);
    } else {
        std::cerr << "unknown or incomplete command '" << verb << "'\n";
        exit_code = 2;
    }

    client.disconnect();
    return exit_code;
}
