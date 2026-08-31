// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Polling + subscriptions (design concept §13/§14): one scheduler batches the
// due datapoints; the application reads from the cache or receives
// typed change callbacks.
//
//   ./example_polling_client <host> <ca> <crt> <key> <kid> <hmac_file>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include <fountainer/client.hpp>
#include <fountainer/datapoints/generated.hpp>

using namespace fountainer;
using namespace std::chrono_literals;

namespace {
volatile std::sig_atomic_t g_stop = 0;
}

int main(int argc, char** argv)
{
    if (argc < 7) {
        std::cerr << "usage: " << argv[0]
                  << " <host> <ca> <crt> <key> <kid> <hmac_file>\n";
        return 2;
    }

    Client client{Endpoint{argv[1]}};
    (void)client.security().set_tls(TlsCredentials::mutual_tls(
        argv[2], argv[3], argv[4],
        EndpointIdentityPolicy::VerifyCertificateChainOnly));
    auto hmac = HmacCredentials::from_file(argv[5], argv[6]);
    if (!hmac) {
        std::cerr << hmac.error().to_string() << '\n';
        return 1;
    }
    (void)client.security().set_hmac(std::move(*hmac));

    auto connection = client.connect();
    if (!connection) {
        std::cerr << connection.error().to_string() << '\n';
        return 1;
    }

    // Spontaneous device events are domain objects, not log lines.
    auto alerts = client.events().on_device_alert([](const DeviceAlert& alert) {
        std::cerr << "ALERT " << alert.code << " " << alert.detail << '\n';
    });

    // Typed value change.
    auto pressure = client.datapoints().subscribe(
        dp::Fon_Current_Pressure, [](const DatapointChange<float>& change) {
            std::cout << "pressure=" << change.value << '\n';
        });

    auto& poll = client.polling();
    poll.every(1s, dp::Fon_Current_Pressure, dp::Fon_Current_State,
               dp::Fon_Relay_Output);
    poll.every(5s, dp::System_RSSI, dp::System_Uptime, dp::Fon_Fault_Code);
    poll.once(dp::Device_Serial_Number, dp::Device_SW_Version);
    poll.start();

    std::signal(SIGINT, [](int) { g_stop = 1; });
    while (!g_stop) std::this_thread::sleep_for(200ms);

    // Cache access: never the network, always immediate (design concept §11.5).
    if (auto cached = client.datapoints().cached(dp::Fon_Current_Pressure)) {
        std::cout << "last pressure: " << cached->value << " ("
                  << to_string(cached->quality) << ")\n";
    }

    const auto stats = poll.stats();
    std::cout << "requests=" << stats.requests
              << " coalesced=" << stats.coalesced_points << '\n';

    (void)client.disconnect();
    return 0;
}
