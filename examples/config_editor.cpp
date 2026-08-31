// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Staging/commit workflow for configuration dialogs (design concept §11.6/§29):
// change values locally, validate client-side, write atomically; the manager
// automatically uses the readback for the cache.
//
//   ./example_config_editor <host> <ca> <crt> <key> <kid> <hmac_file>
#include <iostream>

#include <fountainer/client.hpp>
#include <fountainer/datapoints/generated.hpp>

using namespace fountainer;

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

    auto& dps = client.datapoints();

    // Read the current configuration in one request.
    auto config = dps.read(dp::Fon_Min_Pressure, dp::Fon_Max_Pressure,
                           dp::Fon_Alert_Low_Pressure, dp::Fon_Alert_High_Pressure);
    if (!config) {
        std::cerr << config.error().to_string() << '\n';
        return 1;
    }
    const auto min = config->get(dp::Fon_Min_Pressure);
    const auto max = config->get(dp::Fon_Max_Pressure);
    if (min && max) {
        std::cout << "current min=" << *min << " max=" << *max << '\n';
    }

    // "UI edits": stage first, nothing goes over the network.
    (void)dps.stage(dp::Fon_Min_Pressure, 2.1f);
    (void)dps.stage(dp::Fon_Max_Pressure, 3.6f);

    // Client-side pre-validation (the firmware remains authoritative).
    if (auto validation = dps.validate_staged(); !validation) {
        std::cerr << "validation: " << validation.error().to_string() << '\n';
        dps.discard_staged();
        return 1;
    }

    if (dps.has_staged_changes()) {
        auto commit = dps.commit();   // atomic dp_write batch
        if (!commit) {
            std::cerr << commit.error().to_string() << '\n';
            return 1;
        }
        if (!commit->applied()) {
            std::cerr << "device rejected:\n";
            for (const auto& error : commit->errors) {
                std::cerr << "  " << error.datapoint << ": " << error.reason << '\n';
            }
            return 3;
        }
        std::cout << "applied";
        if (commit->warning) std::cout << " (warning: " << *commit->warning << ")";
        std::cout << '\n';
    }

    (void)client.disconnect();
    return 0;
}
