// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Configuration tests (design concept §60): every violation of the startup
// validation must be caught at load time, not only at the connection attempt.
#include "fountainer/config.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

using fountainer::load_config;

namespace {

// Creates a test directory with dummy certificate files and a
// client.json and cleans it up again at the end of the test.
class ConfigFixture
{
public:
    ConfigFixture()
        : dir_(fs::temp_directory_path() /
               ("fountainer_config_test_" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this))))
    {
        fs::create_directories(dir_);

        touch("ca.crt");
        touch("client.crt");
        touch("client.key");
    }

    ~ConfigFixture()
    {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    std::string path(const std::string& name) const
    {
        return (dir_ / name).string();
    }

    std::string write_config(const std::string& json)
    {
        const auto file = path("client.json");

        std::ofstream stream(file);
        stream << json;

        return file;
    }

    // Valid tls block as a building block
    std::string tls_block() const
    {
        return R"("tls": {
            "ca_file": ")" + path("ca.crt") + R"(",
            "client_cert_file": ")" + path("client.crt") + R"(",
            "client_key_file": ")" + path("client.key") + R"("
        })";
    }

private:
    void touch(const std::string& name)
    {
        std::ofstream stream(path(name));
        stream << "dummy\n";
    }

    fs::path dir_;
};

}  // namespace

TEST_CASE("valid configuration loads with defaults")
{
    ConfigFixture fx;

    const auto config = load_config(fx.write_config(
        R"({ "device": { "host": "192.168.1.51" }, )" + fx.tls_block() +
        "}"));

    CHECK(config.device.host == "192.168.1.51");
    CHECK(config.device.port == 4443);
    CHECK(config.device.path == "/ws");
    CHECK(config.device.subprotocol == "fountain");

    CHECK(config.tls.verify_peer);
    CHECK(config.tls.use_mtls);
    CHECK(config.tls.verify_hostname == "auto");

    CHECK(config.connection.auto_reconnect);
    CHECK(config.connection.connect_timeout == std::chrono::seconds(10));
    CHECK(config.connection.reconnect_initial == std::chrono::seconds(1));
    CHECK(config.connection.reconnect_max == std::chrono::seconds(30));

    CHECK(config.logging.level == "info");
}

TEST_CASE("missing config file fails")
{
    CHECK_THROWS_AS(load_config("/nonexistent/client.json"),
                    std::runtime_error);
}

TEST_CASE("invalid JSON fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(load_config(fx.write_config("{ not json")),
                    std::runtime_error);
}

TEST_CASE("missing host fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "port": 4443 }, )" + fx.tls_block() + "}")),
        std::runtime_error);
}

TEST_CASE("port 0 fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h", "port": 0 }, )" + fx.tls_block() +
            "}")),
        std::runtime_error);
}

TEST_CASE("relative websocket path fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h", "path": "ws" }, )" +
            fx.tls_block() + "}")),
        std::runtime_error);
}

TEST_CASE("missing CA file fails when verify_peer is on")
{
    ConfigFixture fx;

    const auto json = R"({
        "device": { "host": "h" },
        "tls": {
            "ca_file": ")" + fx.path("missing.crt") + R"(",
            "client_cert_file": ")" + fx.path("client.crt") + R"(",
            "client_key_file": ")" + fx.path("client.key") + R"("
        }
    })";

    CHECK_THROWS_AS(load_config(fx.write_config(json)), std::runtime_error);
}

TEST_CASE("missing client certificate fails when mTLS is on")
{
    ConfigFixture fx;

    const auto json = R"({
        "device": { "host": "h" },
        "tls": {
            "ca_file": ")" + fx.path("ca.crt") + R"(",
            "client_key_file": ")" + fx.path("client.key") + R"("
        }
    })";

    CHECK_THROWS_AS(load_config(fx.write_config(json)), std::runtime_error);
}

TEST_CASE("missing client key fails when mTLS is on")
{
    ConfigFixture fx;

    const auto json = R"({
        "device": { "host": "h" },
        "tls": {
            "ca_file": ")" + fx.path("ca.crt") + R"(",
            "client_cert_file": ")" + fx.path("client.crt") + R"("
        }
    })";

    CHECK_THROWS_AS(load_config(fx.write_config(json)), std::runtime_error);
}

TEST_CASE("mTLS off does not require client certificate")
{
    ConfigFixture fx;

    const auto json = R"({
        "device": { "host": "h" },
        "tls": {
            "ca_file": ")" + fx.path("ca.crt") + R"(",
            "use_mtls": false
        }
    })";

    CHECK_NOTHROW(load_config(fx.write_config(json)));
}

TEST_CASE("verify_hostname accepts bool and tri-state strings")
{
    ConfigFixture fx;

    const auto with_verify = [&fx](const std::string& value) {
        return R"({ "device": { "host": "h" }, "tls": {
            "ca_file": ")" + fx.path("ca.crt") + R"(",
            "client_cert_file": ")" + fx.path("client.crt") + R"(",
            "client_key_file": ")" + fx.path("client.key") + R"(",
            "verify_hostname": )" + value + "} }";
    };

    CHECK(load_config(fx.write_config(with_verify("true")))
              .tls.verify_hostname == "on");
    CHECK(load_config(fx.write_config(with_verify("false")))
              .tls.verify_hostname == "off");
    CHECK(load_config(fx.write_config(with_verify("\"auto\"")))
              .tls.verify_hostname == "auto");

    CHECK_THROWS_AS(load_config(fx.write_config(with_verify("\"maybe\""))),
                    std::runtime_error);
}

TEST_CASE("zero timeout fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "connection": { "connect_timeout_s": 0 } })")),
        std::runtime_error);
}

TEST_CASE("reconnect_max below reconnect_initial fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "connection": {
                "reconnect_initial_s": 10, "reconnect_max_s": 5 } })")),
        std::runtime_error);
}

TEST_CASE("unknown log level fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "logging": { "level": "loud" } })")),
        std::runtime_error);
}

TEST_CASE("fountain section absent -> protocol layer disabled")
{
    ConfigFixture fx;

    const auto config = load_config(fx.write_config(
        R"({ "device": { "host": "h" }, )" + fx.tls_block() + " }"));

    CHECK_FALSE(config.fountain.enabled);
}

TEST_CASE("fountain section is parsed with defaults")
{
    ConfigFixture fx;

    {
        std::ofstream key(fx.path("hmac.key"));
        key << "000102030405060708090a0b0c0d0e0f"
               "101112131415161718191a1b1c1d1e1f\n";
    }

    const auto config = load_config(fx.write_config(
        R"({ "device": { "host": "h" }, )" + fx.tls_block() +
        R"(, "fountain": {
            "device_id": "fnt-000001",
            "serial": "00464E5400000001",
            "hmac_key_file": ")" + fx.path("hmac.key") + R"("
        } })"));

    CHECK(config.fountain.enabled);
    CHECK(config.fountain.device_id == "fnt-000001");
    CHECK(config.fountain.serial == "00464E5400000001");
    CHECK(config.fountain.kid == "1");
    CHECK(config.fountain.handshake_timeout == std::chrono::seconds(15));
    CHECK(config.fountain.dp_read_on_ready);
}

TEST_CASE("fountain section without device_id or key file fails")
{
    ConfigFixture fx;

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "fountain": { "hmac_key_file": ")" + fx.path("ca.crt") +
            R"(" } })")),
        std::runtime_error);

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "fountain": {
                "device_id": "fnt-000001",
                "hmac_key_file": ")" + fx.path("missing.key") + R"("
            } })")),
        std::runtime_error);
}

TEST_CASE("idle_timeout defaults to 60s, 0 disables, negative fails")
{
    ConfigFixture fx;

    CHECK(load_config(fx.write_config(
              R"({ "device": { "host": "h" }, )" + fx.tls_block() + " }"))
              .connection.idle_timeout == std::chrono::seconds(60));

    CHECK(load_config(fx.write_config(
              R"({ "device": { "host": "h" }, )" + fx.tls_block() +
              R"(, "connection": { "idle_timeout_s": 0 } })"))
              .connection.idle_timeout == std::chrono::seconds(0));

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "connection": { "idle_timeout_s": -5 } })")),
        std::runtime_error);
}

TEST_CASE("fountain sequence is parsed with per-step defaults")
{
    ConfigFixture fx;
    {
        std::ofstream key(fx.path("hmac.key"));
        key << "000102030405060708090a0b0c0d0e0f"
               "101112131415161718191a1b1c1d1e1f\n";
    }

    const auto config = load_config(fx.write_config(
        R"({ "device": { "host": "h" }, )" + fx.tls_block() +
        R"(, "fountain": {
            "device_id": "fnt-000001",
            "hmac_key_file": ")" + fx.path("hmac.key") + R"(",
            "loop": true,
            "sequence": [
                { "type": "dp_read", "body": { "names": [] } },
                { "type": "dp_write", "body": { "dp": { "Fon_Event_Label": 3 } },
                  "delay_ms": 500, "timeout_s": 4 }
            ]
        } })"));

    REQUIRE(config.fountain.enabled);
    CHECK(config.fountain.loop);
    REQUIRE(config.fountain.sequence.size() == 2);

    const auto& s0 = config.fountain.sequence[0];
    CHECK(s0.type == "dp_read");
    CHECK(s0.delay == std::chrono::milliseconds(0));      // default
    CHECK(s0.timeout == std::chrono::seconds(10));         // default
    CHECK(s0.body.at("names").empty());

    const auto& s1 = config.fountain.sequence[1];
    CHECK(s1.type == "dp_write");
    CHECK(s1.delay == std::chrono::milliseconds(500));
    CHECK(s1.timeout == std::chrono::seconds(4));
    CHECK(s1.body.at("dp").at("Fon_Event_Label") == 3);
}

TEST_CASE("fountain sequence with unknown message type fails")
{
    ConfigFixture fx;
    {
        std::ofstream key(fx.path("hmac.key"));
        key << "000102030405060708090a0b0c0d0e0f"
               "101112131415161718191a1b1c1d1e1f\n";
    }

    CHECK_THROWS_AS(
        load_config(fx.write_config(
            R"({ "device": { "host": "h" }, )" + fx.tls_block() +
            R"(, "fountain": {
                "device_id": "fnt-000001",
                "hmac_key_file": ")" + fx.path("hmac.key") + R"(",
                "sequence": [ { "type": "definitely_not_real" } ]
            } })")),
        std::runtime_error);
}
