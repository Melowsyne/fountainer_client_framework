// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Configuration of the fountainer_client (design concept §9-§11): a JSON file,
// fully validated at startup — errors should surface BEFORE the first
// connection attempt.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fountainer {

struct DeviceConfig {
    std::string host;                 // IP ("192.168.1.101") OR domain
    std::uint16_t port = 4443;        // local firmware_server (plan: 4443)
    std::string path = "/ws";
    std::string subprotocol = "fountain";
};

struct TlsConfig {
    std::string ca_file;
    std::string client_cert_file;
    std::string client_key_file;
    bool verify_peer = true;
    bool use_mtls = true;
    // "auto": hostname verification only for domain endpoints — for an IP
    // the connection is CA-pinned without a hostname check (DHCP device IP,
    // documented strategy in the firmware-server implementation plan).
    // "on"/"off" force the behaviour regardless of the endpoint type.
    std::string verify_hostname = "auto";
};

struct ConnectionConfig {
    bool auto_reconnect = true;
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds reconnect_initial{1};
    std::chrono::seconds reconnect_max{30};
    // WS keepalive (ping at half the timeout, close once it expires without
    // traffic; 0 = off). Detects hard-reset/unpowered devices (half-open TCP).
    std::chrono::seconds idle_timeout{60};
};

struct LoggingConfig {
    std::string level = "info";       // trace|debug|info|warn|error
};

// One step of a test/command sequence (fired after RUNNING). Covers the
// full web-UI surface: dp_read, dp_write, command, log_read, ...
struct FountainStep {
    std::string type;                 // message type (must be listed in the META)
    nlohmann::json body;              // arbitrary request body
    std::chrono::milliseconds delay{0};   // pause BEFORE this step
    std::chrono::seconds timeout{10};     // response timeout (request types)
};

// Fountain v2.2 protocol layer (server role). Section "fountain" in the
// JSON config; if it is absent, the debug handler runs (pure transport level).
struct FountainConfig {
    bool enabled = false;             // true if the section is present
    std::string device_id;            // expected identity ("fnt-000001")
    std::string serial;               // logging/cross-check only ("00464E54...")
    std::string kid = "1";
    std::string hmac_key_file;        // PATH to the key file — NEVER the key
    std::chrono::seconds handshake_timeout{15};
    bool dp_read_on_ready = true;     // full-snapshot smoke test after RUNNING
    std::vector<FountainStep> sequence;   // empty = only the dp_read smoke test
    bool loop = false;                    // restart the sequence after it ends
};

struct Config {
    DeviceConfig device;
    TlsConfig tls;
    ConnectionConfig connection;
    LoggingConfig logging;
    FountainConfig fountain;
};

// Loads and VALIDATES the configuration; throws std::runtime_error with a
// readable message on every violation (design concept §11).
Config load_config(const std::string& path);

}  // namespace fountainer
