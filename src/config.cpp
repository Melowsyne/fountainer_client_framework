// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/config.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "fountainer/protocol/fountain_messages.hpp"

namespace fountainer {

namespace {

void require_file(const std::string& path, const std::string& what)
{
    if (path.empty()) {
        throw std::runtime_error(what + " not configured");
    }
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(what + " does not exist: " + path);
    }
}

std::chrono::seconds read_seconds(const nlohmann::json& json,
                                  const std::string& key,
                                  std::chrono::seconds fallback)
{
    return std::chrono::seconds(
        json.value(key, static_cast<std::int64_t>(fallback.count())));
}

void validate(const Config& config)
{
    if (config.device.host.empty()) {
        throw std::runtime_error("device.host is missing");
    }
    if (config.device.port == 0) {
        throw std::runtime_error("device.port must not be 0");
    }
    if (config.device.path.empty() || config.device.path.front() != '/') {
        throw std::runtime_error(
            "device.path must be a non-empty absolute path: '" +
            config.device.path + "'");
    }

    if (config.tls.verify_peer) {
        require_file(config.tls.ca_file, "tls.ca_file");
    }
    if (config.tls.use_mtls) {
        require_file(config.tls.client_cert_file, "tls.client_cert_file");
        require_file(config.tls.client_key_file, "tls.client_key_file");
    }
    if (config.tls.verify_hostname != "auto" &&
        config.tls.verify_hostname != "on" &&
        config.tls.verify_hostname != "off") {
        throw std::runtime_error(
            "tls.verify_hostname must be \"auto\", \"on\" or \"off\": '" +
            config.tls.verify_hostname + "'");
    }

    if (config.connection.connect_timeout <= std::chrono::seconds::zero()) {
        throw std::runtime_error("connection.connect_timeout_s must be > 0");
    }
    if (config.connection.reconnect_initial <= std::chrono::seconds::zero()) {
        throw std::runtime_error("connection.reconnect_initial_s must be > 0");
    }
    if (config.connection.reconnect_max < config.connection.reconnect_initial) {
        throw std::runtime_error(
            "connection.reconnect_max_s must be >= reconnect_initial_s");
    }
    if (config.connection.idle_timeout < std::chrono::seconds::zero()) {
        throw std::runtime_error(
            "connection.idle_timeout_s must be >= 0 (0 disables keepalive)");
    }

    const auto& level = config.logging.level;
    if (level != "trace" && level != "debug" && level != "info" &&
        level != "warn" && level != "error") {
        throw std::runtime_error(
            "logging.level must be trace|debug|info|warn|error: '" +
            level + "'");
    }

    if (config.fountain.enabled) {
        if (config.fountain.device_id.empty()) {
            throw std::runtime_error("fountain.device_id is missing");
        }
        if (config.fountain.kid.empty()) {
            throw std::runtime_error("fountain.kid is missing");
        }
        // Only existence/path here; the CONTENT is checked by load_hmac_key_file()
        // when the handler starts (also before the first connect).
        require_file(config.fountain.hmac_key_file, "fountain.hmac_key_file");
        if (config.fountain.handshake_timeout <= std::chrono::seconds::zero()) {
            throw std::runtime_error("fountain.handshake_timeout_s must be > 0");
        }
        for (const auto& step : config.fountain.sequence) {
            if (protocol::find_meta(step.type) == nullptr) {
                throw std::runtime_error(
                    "fountain.sequence: unknown message type '" + step.type +
                    "'");
            }
        }
    }
}

}  // namespace

Config load_config(const std::string& path)
{
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open config file: " + path);
    }

    Config config;

    try {
        const auto json = nlohmann::json::parse(stream,
                                                /*callback=*/nullptr,
                                                /*allow_exceptions=*/true,
                                                /*ignore_comments=*/true);

        if (json.contains("device")) {
            const auto& device = json.at("device");

            config.device.host = device.value("host", "");

            const int port = device.value("port", 4443);
            if (port < 0 || port > 65535) {
                throw std::runtime_error("device.port out of range: " +
                                         std::to_string(port));
            }
            config.device.port = static_cast<std::uint16_t>(port);

            config.device.path = device.value("path", "/ws");
            config.device.subprotocol =
                device.value("websocket_subprotocol", "fountain");
        }

        if (json.contains("tls")) {
            const auto& tls = json.at("tls");

            config.tls.ca_file = tls.value("ca_file", "");
            config.tls.client_cert_file = tls.value("client_cert_file", "");
            config.tls.client_key_file = tls.value("client_key_file", "");
            config.tls.verify_peer = tls.value("verify_peer", true);
            config.tls.use_mtls = tls.value("use_mtls", true);

            // Backwards compatible: a JSON bool is mapped to "on"/"off"
            if (tls.contains("verify_hostname")) {
                const auto& raw = tls.at("verify_hostname");
                if (raw.is_boolean()) {
                    config.tls.verify_hostname =
                        raw.get<bool>() ? "on" : "off";
                } else {
                    config.tls.verify_hostname = raw.get<std::string>();
                }
            }
        }

        if (json.contains("connection")) {
            const auto& connection = json.at("connection");

            config.connection.auto_reconnect =
                connection.value("auto_reconnect", true);
            config.connection.connect_timeout =
                read_seconds(connection, "connect_timeout_s",
                             config.connection.connect_timeout);
            config.connection.reconnect_initial =
                read_seconds(connection, "reconnect_initial_s",
                             config.connection.reconnect_initial);
            config.connection.reconnect_max =
                read_seconds(connection, "reconnect_max_s",
                             config.connection.reconnect_max);
            config.connection.idle_timeout =
                read_seconds(connection, "idle_timeout_s",
                             config.connection.idle_timeout);
        }

        if (json.contains("logging")) {
            config.logging.level = json.at("logging").value("level", "info");
        }

        if (json.contains("fountain")) {
            const auto& fountain = json.at("fountain");

            config.fountain.enabled = true;
            config.fountain.device_id = fountain.value("device_id", "");
            config.fountain.serial = fountain.value("serial", "");
            config.fountain.kid = fountain.value("kid", "1");
            config.fountain.hmac_key_file =
                fountain.value("hmac_key_file", "");
            config.fountain.handshake_timeout =
                read_seconds(fountain, "handshake_timeout_s",
                             config.fountain.handshake_timeout);
            config.fountain.dp_read_on_ready =
                fountain.value("dp_read_on_ready", true);
            config.fountain.loop = fountain.value("loop", false);

            if (fountain.contains("sequence")) {
                for (const auto& entry : fountain.at("sequence")) {
                    FountainStep step;
                    step.type = entry.value("type", "");
                    step.body = entry.value("body", nlohmann::json::object());
                    step.delay = std::chrono::milliseconds(
                        entry.value("delay_ms", 0));
                    step.timeout = std::chrono::seconds(
                        entry.value("timeout_s", 10));
                    config.fountain.sequence.push_back(std::move(step));
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("invalid JSON in " + path + ": " + e.what());
    }

    validate(config);

    return config;
}

}  // namespace fountainer
