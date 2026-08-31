// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Test.cpp — extensive HARDWARE test against a real device, entirely
// through the public API (design concept §25: tests use the same API as the
// later application).
//
//   hardware_test <config.json>
//
// Sequence (safe matrix, design concept §25.1/§26):
//   1  Connection: connect() == Fountain RUNNING, ConnectionInfo
//   2  connect() idempotency (already Ready -> immediate response)
//   3  read_all(): all datapoints, plausibility per datapoint
//   4  Typed reads, cache semantics
//   5  Save baseline of all RW datapoints (RestoreGuard)
//   6  Same-value writes (network datapoints: safe, real mutation only in the lab)
//   7  LIMITS: all numeric RW datapoints atomically to min, then max
//   8  Negative tests (firmware validation via raw): unknown_name, read_only,
//      type_mismatch, out_of_range, too_long; batch atomicity
//   9  Staging/commit
//  10  Commands: set_state Off/Auto, unknown command
//  11  Logs: read, read_previous, read_all (pagination), flush
//  12  Polling + subscriptions (realtime values, change callbacks)
//  13  Request timeout as a typed error
//  14  Restore baseline + final comparison
//
// NOT included (deliberately, without explicit approval): reboot, wd_fault,
// link_fault, Network_Save 1-4, Log_Command 1/2, real network mutation,
// firmware upgrade.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

#include <fountainer/client.hpp>
#include <fountainer/config.hpp>
#include <fountainer/datapoints/constraints.hpp>
#include <fountainer/datapoints/generated.hpp>
#include <fountainer/datapoints/restore_guard.hpp>
#include <fountainer/logging/logger.hpp>

using namespace fountainer;
using namespace std::chrono_literals;

namespace {

// ---------------------------------------------------------------------------
// Mini test harness
// ---------------------------------------------------------------------------

struct Score {
    int pass = 0;
    int fail = 0;
    int warn = 0;
    int skip = 0;
} g_score;

void check(bool ok, const std::string& what, const std::string& detail = {})
{
    if (ok) {
        g_score.pass++;
        std::printf("  PASS  %s\n", what.c_str());
    } else {
        g_score.fail++;
        std::printf("  FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : " — ",
                    detail.c_str());
    }
}

void warn(const std::string& what, const std::string& detail = {})
{
    g_score.warn++;
    std::printf("  WARN  %s%s%s\n", what.c_str(), detail.empty() ? "" : " — ",
                detail.c_str());
}

void skip(const std::string& what)
{
    g_score.skip++;
    std::printf("  SKIP  %s\n", what.c_str());
}

void section(const char* title) { std::printf("\n== %s ==\n", title); }

std::string describe(const Result<WriteResult>& result)
{
    if (!result.has_value()) return result.error().to_string();
    if (result->applied()) return "applied";
    std::string out = "rejected:";
    for (const auto& error : result->errors) {
        out += " " + error.datapoint + "=" + error.reason;
    }
    return out;
}

double as_double(const DatapointValue& value)
{
    return std::visit(
        [](const auto& typed) -> double {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, std::string>) return 0.0;
            else return static_cast<double>(typed);
        },
        value);
}

// Datapoints that are NEVER written automatically (command/network datapoints
// and pump control — design concept §26).
const std::set<std::string> kNoWrite = {
    "Network_Save", "Log_Command",
};
const std::set<std::string> kSameValueOnly = {
    "Network_SSID",       "Network_Server_Port", "Network_Server",
    "Network_Password",   "Network_DHCP",        "Network_IP_Address",
    "Network_Subnetmask", "Network_Gateway",     "Fon_Pressure_Manual",
    "Fon_Pressure_Value", "Fon_Fault_Ack",
};

// The pressure chain is cross-field coupled (AlertLow < Min < Max < AlertHigh,
// dp_constraints_ok in the firmware) — pure per-datapoint extremes are
// self-contradictory there and are tested as a CHAIN.
const std::set<std::string> kConstraintChain = {
    "Fon_Alert_Low_Pressure", "Fon_Min_Pressure", "Fon_Max_Pressure",
    "Fon_Alert_High_Pressure",
};

bool boundary_candidate(const DatapointDescriptor& d)
{
    if (d.access != Access::ReadWrite) return false;
    if (!d.numeric()) return false;
    if (std::isnan(d.min) || std::isnan(d.max)) return false;
    if (kNoWrite.count(std::string(d.name)) != 0) return false;
    if (kSameValueOnly.count(std::string(d.name)) != 0) return false;
    if (kConstraintChain.count(std::string(d.name)) != 0) return false;
    return true;
}

Result<DatapointValue> numeric_value(const DatapointDescriptor& d, double number)
{
    return value_from_json(d, d.type == DatapointType::F32
                                  ? nlohmann::json(number)
                                  : nlohmann::json(static_cast<std::int64_t>(number)));
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <config.json>\n";
        return 2;
    }

    Config config;
    try {
        config = load_config(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "config: " << e.what() << '\n';
        return 2;
    }
    log::set_level(log::Level::Warn);   // the test output should dominate

    // -----------------------------------------------------------------------
    section("1. Connection (builder API, connect == Fountain RUNNING)");
    // -----------------------------------------------------------------------

    auto hmac = HmacCredentials::from_file(config.fountain.kid,
                                           config.fountain.hmac_key_file);
    if (!hmac) {
        std::cerr << hmac.error().to_string() << '\n';
        return 2;
    }

    auto built =
        Client::builder(Endpoint{config.device.host, config.device.port,
                                 config.device.path, config.device.subprotocol})
            .with_tls(TlsCredentials::mutual_tls(
                config.tls.ca_file, config.tls.client_cert_file,
                config.tls.client_key_file,
                EndpointIdentityPolicy::VerifyCertificateChainOnly))
            .with_hmac(std::move(*hmac))
            .with_expected_device(config.fountain.device_id)
            .with_reconnect({.enabled = true})
            .build();
    check(built.has_value(), "builder creates the Client",
          built ? "" : built.error().to_string());
    if (!built) return 1;
    Client client = std::move(*built);

    const auto t_connect = std::chrono::steady_clock::now();
    auto info = client.connect();
    check(info.has_value(), "connect() reaches RUNNING",
          info ? "" : info.error().to_string());
    if (!info) return 1;
    const auto connect_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t_connect)
                                .count();
    std::printf("        device=%s serial=%s fw=%s hw=%s (%lld ms)\n",
                info->device_id.c_str(), info->serial.c_str(),
                info->firmware_version.c_str(), info->hardware_revision.c_str(),
                static_cast<long long>(connect_ms));

    check(info->device_id == config.fountain.device_id, "ConnectionInfo.device_id");
    check(info->serial == config.fountain.serial, "ConnectionInfo.serial",
          info->serial + " != " + config.fountain.serial);
    check(info->protocol_version == 2, "protocol version v2");
    check(!info->firmware_version.empty(), "firmware version present");
    check(info->auth_scheme == "hmac-sha256" && info->auth_kid == config.fountain.kid,
          "auth scheme/KID negotiated");
    check(client.ready() && client.state() == ClientState::Ready,
          "ClientState::Ready");

    // -----------------------------------------------------------------------
    section("2. connect() idempotency");
    // -----------------------------------------------------------------------
    auto again = client.connect();
    check(again.has_value() && again->device_id == info->device_id,
          "repeated connect() returns the same ConnectionInfo immediately");

    // -----------------------------------------------------------------------
    section("3. read_all(): completeness + plausibility of all datapoints");
    // -----------------------------------------------------------------------
    auto all = client.datapoints().read_all();
    check(all.has_value(), "read_all()", all ? "" : all.error().to_string());
    if (!all) return 1;

    check(all->size() == kDatapointCount,
          "alle " + std::to_string(kDatapointCount) + " datapoints delivered",
          "received: " + std::to_string(all->size()) + ", unknown: " +
              std::to_string(all->unknown().size()));

    std::size_t missing = 0, bounds_violations = 0;
    for (const auto& descriptor : catalog::all()) {
        const DatapointValue* value = all->find(descriptor.name);
        if (value == nullptr) {
            missing++;
            std::printf("        MISSING: %.*s\n", int(descriptor.name.size()),
                        descriptor.name.data());
            continue;
        }
        // The type is correct (otherwise the codec would have rejected it).
        // Additionally: is the value within its own catalog value range?
        if (descriptor.numeric() &&
            (!std::isnan(descriptor.min) || !std::isnan(descriptor.max))) {
            const double number = as_double(*value);
            if ((!std::isnan(descriptor.min) && number < descriptor.min) ||
                (!std::isnan(descriptor.max) && number > descriptor.max)) {
                bounds_violations++;
                warn(std::string(descriptor.name) + " outside [min,max]",
                     to_display_string(descriptor, *value));
            }
        }
    }
    check(missing == 0, "no datapoint is missing");
    if (bounds_violations == 0) check(true, "all values within their catalog bounds");

    // Targeted plausibility of individual values
    {
        auto serial = all->get(dp::Device_Serial_Number);
        char expected[17];
        std::snprintf(expected, sizeof expected, "%016llX",
                      std::strtoull(config.fountain.serial.c_str(), nullptr, 16));
        check(serial && *serial == std::strtoull(expected, nullptr, 16),
              "Device_Serial_Number == configuration");

        auto sw = all->get(dp::Device_SW_Version);
        check(sw && *sw == info->firmware_version,
              "Device_SW_Version == hello.fw_version",
              sw ? *sw + " != " + info->firmware_version : "missing");

        auto build = all->get(dp::Device_Build_Version);
        check(build && *build > 1'500'000'000'000ULL && *build < 2'500'000'000'000ULL,
              "Device_Build_Version is a plausible ms Unix timestamp");

        auto uptime = all->get(dp::System_Uptime);
        check(uptime && *uptime > 0 && *uptime < 90u * 24 * 3600,
              "System_Uptime plausible (0 < t < 90 days)",
              uptime ? std::to_string(*uptime) + " s" : "missing");

        auto mem = all->get(dp::System_Memory_Free);
        check(mem && *mem > 10'000 && *mem < 20'000'000,
              "System_Memory_Free plausible",
              mem ? std::to_string(*mem) + " B" : "missing");

        auto rssi = all->get(dp::System_RSSI);
        check(rssi && *rssi >= -100 && *rssi < 0, "System_RSSI plausible (-100..0)",
              rssi ? std::to_string(*rssi) + " dBm" : "missing");

        auto temperature = all->get(dp::System_Temperature);
        check(temperature && *temperature > -20.0f && *temperature < 90.0f,
              "System_Temperature plausible",
              temperature ? std::to_string(*temperature) : "missing");

        auto link = all->get(dp::Net_Link_Score);
        check(link && *link <= 100, "Net_Link_Score 0..100");

        auto pressure = all->get(dp::Fon_Current_Pressure);
        check(pressure && *pressure > -1.0f && *pressure < 50.0f,
              "Fon_Current_Pressure plausible",
              pressure ? std::to_string(*pressure) + " bar" : "missing");
    }

    // -----------------------------------------------------------------------
    section("4. Typed reads + cache semantics");
    // -----------------------------------------------------------------------
    auto uptime1 = client.datapoints().read(dp::System_Uptime);
    check(uptime1.has_value(), "typed single read (Result<uint32_t>)");

    auto multi = client.datapoints().read(dp::System_Uptime, dp::System_RSSI,
                                          dp::Fon_Current_State);
    check(multi.has_value() && multi->size() == 3,
          "typed multi-read in ONE request");

    std::this_thread::sleep_for(1100ms);
    auto uptime2 = client.datapoints().read(dp::System_Uptime);
    check(uptime1 && uptime2 && *uptime2 >= *uptime1,
          "System_Uptime monotonically increasing",
          uptime1 && uptime2
              ? std::to_string(*uptime1) + " -> " + std::to_string(*uptime2)
              : "read failed");

    auto cached = client.datapoints().cached(dp::System_Uptime);
    check(cached && uptime2 && cached->value == *uptime2 &&
              cached->quality == DataQuality::Good &&
              cached->source == DatapointSource::ExplicitRead,
          "cached() returns the last read without touching the network");

    auto dynamic_read = client.datapoints().read("Fon_Current_State");
    check(dynamic_read.has_value(), "dynamic runtime API read(\"name\")");

    // -----------------------------------------------------------------------
    section("5. Saving the baseline of all RW datapoints (RestoreGuard)");
    // -----------------------------------------------------------------------
    auto guard = DatapointRestoreGuard::capture_all_writable(
        client.datapoints(), {"Network_Save", "Log_Command"});
    check(guard.has_value(), "baseline saved",
          guard ? std::to_string(guard->baseline().size()) + " points"
                : guard.error().to_string());
    if (!guard) return 1;

    // -----------------------------------------------------------------------
    section("6. Same-value writes (safe network/pump datapoints)");
    // -----------------------------------------------------------------------
    {
        DatapointWriteSet same;
        std::size_t count = 0;
        for (const auto& name : kSameValueOnly) {
            const DatapointValue* value = guard->baseline().find(name);
            if (value == nullptr) continue;
            if (auto status = same.set(name, *value); status) count++;
        }
        auto written = client.datapoints().write(same);
        check(written.has_value() && written->applied(),
              "same-value batch (" + std::to_string(count) + " points) applied",
              describe(written));
        if (written && written->warning) {
            warn("dp_write warning", *written->warning);
        }
        if (written && written->applied()) {
            bool readback_ok = true;
            for (const auto& [name, value] : same.values()) {
                const DatapointValue* echoed = written->readback.find(name);
                if (echoed == nullptr || *echoed != value) readback_ok = false;
            }
            check(readback_ok, "readback identical to the written values");
        }
    }

    // -----------------------------------------------------------------------
    section("7. LIMITS: all numeric RW datapoints to min and max");
    // -----------------------------------------------------------------------
    {
        std::vector<const DatapointDescriptor*> candidates;
        for (const auto& descriptor : catalog::all()) {
            if (boundary_candidate(descriptor)) candidates.push_back(&descriptor);
        }
        std::printf("        %zu boundary candidates\n", candidates.size());
        check(candidates.size() >= 25, "enough boundary candidates found");

        for (const double edge : {0.0, 1.0}) {   // 0.0 = min, 1.0 = max
            DatapointWriteSet batch;
            for (const auto* d : candidates) {
                const double target = edge == 0.0 ? d->min : d->max;
                auto value = numeric_value(*d, target);
                if (!value) {
                    warn("boundary value not encodable", std::string(d->name));
                    continue;
                }
                (void)batch.set(std::string(d->name), std::move(*value));
            }
            auto written = client.datapoints().write(batch);
            const char* label = edge == 0.0 ? "MIN" : "MAX";
            check(written.has_value() && written->applied(),
                  std::string("atomic ") + label + "-batch applied",
                  describe(written));
            if (written && written->applied()) {
                std::size_t mismatches = 0;
                for (const auto& [name, value] : batch.values()) {
                    const DatapointValue* echoed = written->readback.find(name);
                    if (echoed == nullptr || *echoed != value) {
                        mismatches++;
                        warn(std::string(label) + "-readback deviates", name);
                    }
                }
                check(mismatches == 0,
                      std::string(label) + "-readback of all points exact");
            }
        }

        // Out of bounds: testing max+step individually for every candidate
        // would be slow — representative selection across the types.
        struct OorProbe {
            const DatapointDescriptor* d;
            double value;
        };
        std::vector<OorProbe> probes;
        for (const auto* d : candidates) {
            const double step = d->type == DatapointType::F32 ? 0.5 : 1.0;
            probes.push_back({d, d->max + step});
            if (d->min > 0 || d->type == DatapointType::I16) {
                probes.push_back({d, d->min - step});
            }
            if (probes.size() >= 10) break;
        }
        std::size_t rejected = 0;
        for (const auto& probe : probes) {
            // raw: the client-side pre-validation would otherwise catch this —
            // here the FIRMWARE is supposed to reject it (design concept §11.7).
            protocol::RequestSpec spec;
            spec.type = "dp_write";
            spec.body = {{"dp", {{std::string(probe.d->name), probe.value}}}};
            auto response = client.raw().request(std::move(spec));
            if (response && response->value("status", "") == "rejected") rejected++;
        }
        check(rejected == probes.size(),
              "firmware rejects out_of_range (" + std::to_string(rejected) + "/" +
                  std::to_string(probes.size()) + ")");

        // --- Pressure chain: cross-field-compliant extremes as a CHAIN ----
        {
            DatapointWriteSet chain_min;
            chain_min.set(dp::Fon_Alert_Low_Pressure, 0.0f);
            chain_min.set(dp::Fon_Min_Pressure, 0.01f);
            chain_min.set(dp::Fon_Max_Pressure, 0.02f);
            chain_min.set(dp::Fon_Alert_High_Pressure, 0.03f);
            auto low = client.datapoints().write(chain_min);
            check(low.has_value() && low->applied(),
                  "pressure chain near minimum (ascending) applied", describe(low));

            DatapointWriteSet chain_max;
            chain_max.set(dp::Fon_Alert_Low_Pressure, 9.97f);
            chain_max.set(dp::Fon_Min_Pressure, 9.98f);
            chain_max.set(dp::Fon_Max_Pressure, 9.99f);
            chain_max.set(dp::Fon_Alert_High_Pressure, 12.0f);
            auto high = client.datapoints().write(chain_max);
            check(high.has_value() && high->applied(),
                  "pressure chain near maximum (ascending) applied", describe(high));

            // Broken chain: the firmware must return constraint_violation —
            // and the client-side pre-validation must see the same BEFORE the network.
            DatapointWriteSet broken;
            broken.set(dp::Fon_Min_Pressure, 3.0f);
            broken.set(dp::Fon_Max_Pressure, 2.0f);
            auto pre = client.datapoints().validate_constraints(broken);
            check(!pre.has_value() &&
                      pre.error().code == ErrorCode::ConstraintViolation &&
                      pre.error().datapoint == "Fon_Max_Pressure",
                  "client-side pre-validation reports the constraint violation "
                  "(same field naming as the firmware)");

            protocol::RequestSpec spec;
            spec.type = "dp_write";
            spec.body = {{"dp",
                          {{"Fon_Min_Pressure", 3.0}, {"Fon_Max_Pressure", 2.0}}}};
            auto remote = client.raw().request(std::move(spec));
            check(remote && remote->value("status", "") == "rejected" &&
                      (*remote)["errors"].value("Fon_Max_Pressure", "") ==
                          "constraint_violation",
                  "firmware rejects the constraint violation "
                  "(Fon_Min >= Fon_Max)");
        }
    }

    // -----------------------------------------------------------------------
    section("8. Negative tests (firmware validation) + batch atomicity");
    // -----------------------------------------------------------------------
    {
        auto raw_write = [&](nlohmann::json dp) {
            protocol::RequestSpec spec;
            spec.type = "dp_write";
            spec.body = {{"dp", std::move(dp)}};
            return client.raw().request(std::move(spec));
        };

        auto unknown = raw_write({{"Totally_Unknown_DP", 1}});
        check(unknown && unknown->value("status", "") == "rejected" &&
                  (*unknown)["errors"].value("Totally_Unknown_DP", "") ==
                      "unknown_name",
              "unknown_name is rejected");

        auto read_only = raw_write({{"Fon_Current_Pressure", 1.0}});
        check(read_only && read_only->value("status", "") == "rejected" &&
                  (*read_only)["errors"].value("Fon_Current_Pressure", "") ==
                      "read_only",
              "read_only is rejected");

        auto type_mismatch = raw_write({{"Fon_Min_Pressure", "hello"}});
        check(type_mismatch && type_mismatch->value("status", "") == "rejected" &&
                  (*type_mismatch)["errors"].value("Fon_Min_Pressure", "") ==
                      "type_mismatch",
              "type_mismatch is rejected");

        auto too_long = raw_write({{"Network_SSID", std::string(70, 'x')}});
        check(too_long && too_long->value("status", "") == "rejected" &&
                  (*too_long)["errors"].value("Network_SSID", "") == "too_long",
              "too_long (>63 characters) is rejected");

        // Client-side pre-validation (the typed API catches it earlier):
        auto client_side = client.datapoints().write(dp::Fon_Min_Pressure, 99.0f);
        check(!client_side.has_value() &&
                  client_side.error().code == ErrorCode::OutOfRange,
              "typed API validates BEFORE the network (OutOfRange)");

        // Atomicity: valid + invalid in one batch -> NOTHING gets
        // applied.
        const auto label_before = client.datapoints().read(dp::Fon_Event_Label);
        auto atomic = raw_write(
            {{"Fon_Event_Label", 3}, {"Fon_Min_Pressure", 99.0}});
        const auto label_after = client.datapoints().read(dp::Fon_Event_Label);
        check(atomic && atomic->value("status", "") == "rejected" &&
                  label_before && label_after && *label_before == *label_after,
              "batch is atomic: the valid part is NOT applied on "
              "rejection");

        // dp_read with an unknown name: NO error, no abort — the local
        // server silently omits unknown names (dp_read_into with
        // unknown_out=NULL, local_protocol.c); the cloud additionally returns
        // an unknown list. Both variants conform to the contract.
        protocol::RequestSpec read_spec;
        read_spec.type = "dp_read";
        read_spec.body = {{"names", {"System_Uptime", "Totally_Unknown_DP"}}};
        auto read_unknown = client.raw().request(std::move(read_spec));
        const bool tolerated =
            read_unknown && (*read_unknown)["dp"].contains("System_Uptime") &&
            !(*read_unknown)["dp"].contains("Totally_Unknown_DP");
        check(tolerated,
              "dp_read tolerates unknown names (known ones are delivered)");
    }

    // -----------------------------------------------------------------------
    section("9. Staging/commit (configuration-editor workflow)");
    // -----------------------------------------------------------------------
    {
        auto& dps = client.datapoints();

        // The limit tests left the pressure chain at ~10 bar — a partial
        // staging (only Min/Max) would violate the cross-field rules.
        // So stage the COMPLETE chain (real editor case).
        (void)dps.stage(dp::Fon_Alert_Low_Pressure, 0.3f);
        (void)dps.stage(dp::Fon_Min_Pressure, 2.1f);
        (void)dps.stage(dp::Fon_Max_Pressure, 3.6f);
        (void)dps.stage(dp::Fon_Alert_High_Pressure, 4.5f);
        check(dps.has_staged_changes() && dps.validate_staged().has_value(),
              "stage() + validate_staged() (incl. cross-field rules)");

        // Cross-check: a broken chain already fails in validate_staged().
        (void)dps.stage(dp::Fon_Max_Pressure, 1.0f);   // < Min=2.1
        auto invalid = dps.validate_staged();
        check(!invalid.has_value() &&
                  invalid.error().code == ErrorCode::ConstraintViolation,
              "validate_staged() detects the cross-field violation before the network");
        (void)dps.stage(dp::Fon_Max_Pressure, 3.6f);   // repair

        auto committed = dps.commit();
        check(committed.has_value() && committed->applied(),
              "commit() writes atomically", describe(committed));
        check(!dps.has_staged_changes(), "staging cleared after applied");

        auto verify = dps.read(dp::Fon_Min_Pressure);
        check(verify && std::abs(*verify - 2.1f) < 1e-4, "commit confirmed by a read");
        // The chain is left in a consistent state; the guard restores the baseline.
    }

    // -----------------------------------------------------------------------
    section("10. Commands");
    // -----------------------------------------------------------------------
    {
        auto off = client.commands().set_state(FountainState::Off);
        check(off.has_value() && off->applied(), "set_state(Off) applied",
              off ? (off->error ? *off->error : "") : off.error().to_string());

        std::this_thread::sleep_for(500ms);
        auto relay = client.datapoints().read(dp::Fon_Relay_Output);
        check(relay.has_value() && *relay == false, "relay off after Off");

        auto autos = client.commands().set_state(FountainState::Auto);
        check(autos.has_value() && autos->applied(),
              "set_state(Auto) applied (normal state restored)");

        auto bogus = client.commands().raw("frobnicate", std::nullopt, std::nullopt);
        check(bogus.has_value() && !bogus->applied() &&
                  bogus->error == "unknown_command",
              "unknown command -> rejected/unknown_command (no "
              "transport error)");

        skip("reboot / wd_fault / link_fault / turn_on (only with explicit "
             "approval)");
    }

    // -----------------------------------------------------------------------
    section("11. Logs (Pull, Pagination, prev-Boot, flush)");
    // -----------------------------------------------------------------------
    {
        auto batch = client.logs().read({.max_records = 16});
        check(batch.has_value(), "log_read",
              batch ? std::to_string(batch->records.size()) + " records, next_seq=" +
                          std::to_string(batch->next_seq)
                    : batch.error().to_string());
        if (batch) {
            bool ascending = true;
            for (std::size_t i = 1; i < batch->records.size(); i++) {
                if (batch->records[i].sequence <= batch->records[i - 1].sequence) {
                    ascending = false;
                }
            }
            check(ascending, "log sequences strictly increasing");
            bool levels_ok = true;
            for (const auto& record : batch->records) {
                if (record.level > 5) levels_ok = false;
            }
            check(levels_ok, "log levels within the valid range (0..5)");
        }

        auto everything = client.logs().read_all();
        check(everything.has_value(), "read_all() paginates to completion",
              everything ? std::to_string(everything->size()) + " records"
                         : everything.error().to_string());
        if (everything && batch) {
            check(everything->size() >= batch->records.size(),
                  "read_all() delivers at least as much as one page");
        }

        auto prev_flag = client.datapoints().read(dp::Log_Prev_Boot_Available);
        auto previous = client.logs().read_previous({.max_records = 8});
        check(previous.has_value(), "log_read_prev");
        if (previous && prev_flag) {
            check(previous->previous_boot_available == *prev_flag,
                  "available flag == Log_Prev_Boot_Available",
                  std::string("batch=") +
                      (previous->previous_boot_available ? "true" : "false") +
                      " dp=" + (*prev_flag ? "true" : "false"));
        }
        skip("log ack_previous / clear_runtime (destructive)");

        auto flushed = client.logs().flush();   // Log_Command=3: compatible no-op
        check(flushed.has_value(), "logs().flush() (Log_Command=3)");
    }

    // -----------------------------------------------------------------------
    section("12. Polling + Subscriptions");
    // -----------------------------------------------------------------------
    {
        std::atomic<int> changes{0};
        auto subscription = client.datapoints().subscribe(
            dp::System_Uptime,
            [&](const DatapointChange<std::uint32_t>&) { changes++; });

        auto& poll = client.polling();
        poll.every(500ms, dp::Fon_Current_Pressure, dp::Fon_Current_State,
                   dp::System_Uptime);
        poll.every(2s, dp::System_RSSI, dp::Fon_Fault_Code);
        poll.start();
        // 6.5 s: on the firmware side System_Uptime is only updated in the 5 s
        // monitor cycle (main_monitor_cycle) — this guarantees >=1 update.
        std::this_thread::sleep_for(6500ms);
        poll.stop();

        const auto stats = poll.stats();
        std::printf("        requests=%llu coalesced=%llu skipped=%llu "
                    "missed=%llu failures=%llu\n",
                    (unsigned long long)stats.requests,
                    (unsigned long long)stats.coalesced_points,
                    (unsigned long long)stats.skipped_inflight,
                    (unsigned long long)stats.missed_deadlines,
                    (unsigned long long)stats.failures);
        check(stats.requests >= 6, "poller sent coalesced requests");
        check(stats.coalesced_points >= 2 * stats.requests,
              "coalescing: several points per request");
        check(stats.failures == 0, "no poll failures");
        // The 5 s monitor cycle guarantees at least one uptime update within
        // the 6.5 s window; the polling (500 ms) carries it into the cache.
        check(changes.load() >= 1, "uptime subscription fired",
              std::to_string(changes.load()) + " changes in 6.5 s");

        auto cached_pressure = client.datapoints().cached(dp::Fon_Current_Pressure);
        check(cached_pressure &&
                  cached_pressure->source == DatapointSource::PeriodicPoll,
              "cache source = PeriodicPoll");
    }

    // -----------------------------------------------------------------------
    section("13. Request timeout as a typed error");
    // -----------------------------------------------------------------------
    {
        // Deterministic: first fill max_in_flight (4), then enqueue a request
        // with a 1 ms deadline — it expires in the QUEUE, regardless of how
        // quickly the device responds.
        for (int i = 0; i < 4; i++) {
            protocol::RequestSpec filler;
            filler.type = "dp_read";
            filler.body = {{"names", {"System_Uptime"}}};
            client.raw().async_request(std::move(filler), [](auto) {});
        }
        protocol::RequestSpec spec;
        spec.type = "dp_read";
        spec.body = {{"names", {"System_Uptime"}}};
        spec.timeout = 1ms;
        auto response = client.raw().request(std::move(spec));
        check(!response.has_value() &&
                  response.error().code == ErrorCode::RequestTimeout &&
                  response.error().domain == ErrorDomain::Timeout,
              "1 ms deadline -> ErrorCode::RequestTimeout (no null JSON)",
              response.has_value() ? "a response got through"
                                   : response.error().to_string());
    }

    // -----------------------------------------------------------------------
    section("14. Restore baseline + final comparison");
    // -----------------------------------------------------------------------
    {
        auto restored = guard->restore();
        check(restored.has_value() && restored->applied(),
              "baseline restored atomically", describe(restored));

        // Final read against the baseline.
        std::vector<std::string> names;
        for (const auto& [name, value] : guard->baseline().values()) {
            (void)value;
            names.push_back(name);
        }
        auto final_state = client.datapoints().read_names(names);
        std::size_t diffs = 0;
        if (final_state) {
            for (const auto& [name, value] : guard->baseline().values()) {
                const DatapointValue* now = final_state->find(name);
                if (now == nullptr || *now != value) {
                    diffs++;
                    warn("baseline deviation after restore", name);
                }
            }
        }
        check(final_state.has_value() && diffs == 0,
              "final readback == baseline (" +
                  std::to_string(guard->baseline().size()) + " points)");
    }

    const auto metrics = client.metrics();
    std::printf("\nDispatcher: sent=%llu completed=%llu timeouts=%llu "
                "unexpected=%llu deferrals=%llu\n",
                (unsigned long long)metrics.sent,
                (unsigned long long)metrics.completed,
                (unsigned long long)metrics.timeouts,
                (unsigned long long)metrics.unexpected_responses,
                (unsigned long long)metrics.rate_limit_deferrals);

    auto disconnected = client.disconnect();
    check(disconnected.has_value() && client.state() == ClientState::Disconnected,
          "disconnect() waits until Disconnected");

    std::printf("\n==================== RESULT ====================\n");
    std::printf("PASS: %d   FAIL: %d   WARN: %d   SKIP: %d\n", g_score.pass,
                g_score.fail, g_score.warn, g_score.skip);
    return g_score.fail == 0 ? 0 : 1;
}
