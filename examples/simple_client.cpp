// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Minimal example (design concept §1): connect, read one value typed,
// write one value, disconnect cleanly.
//
//   ./example_simple_client <host> <ca.pem> <client.crt> <client.key>
//       <kid> <hmac_key_file> [device_id]
#include <iostream>

#include <fountainer/client.hpp>
#include <fountainer/datapoints/generated.hpp>

using namespace fountainer;

int main(int argc, char** argv)
{
    if (argc < 7) {
        std::cerr << "usage: " << argv[0]
                  << " <host> <ca> <crt> <key> <kid> <hmac_file> [device_id]\n";
        return 2;
    }

    Client client{Endpoint{argv[1]}};

    // Local device IP without a DNS name: verify the chain against the private
    // CA — explicitly, not via heuristics (design concept §9.2).
    auto tls_status = client.security().set_tls(TlsCredentials::mutual_tls(
        argv[2], argv[3], argv[4],
        EndpointIdentityPolicy::VerifyCertificateChainOnly));
    if (!tls_status) {
        std::cerr << tls_status.error().to_string() << '\n';
        return 1;
    }

    if (argc > 7) client.security().set_expected_device_id(argv[7]);

    auto hmac = HmacCredentials::from_file(argv[5], argv[6]);
    if (!hmac) {
        std::cerr << hmac.error().to_string() << '\n';
        return 1;
    }
    (void)client.security().set_hmac(std::move(*hmac));

    // connect() only succeeds once Fountain is RUNNING (design concept §8.2).
    auto connection = client.connect();
    if (!connection) {
        std::cerr << connection.error().to_string() << '\n';
        return 1;
    }
    std::cout << "device=" << connection->device_id
              << " fw=" << connection->firmware_version << '\n';

    // Typed read: Result<float>, no JSON.
    auto pressure = client.datapoints().read(dp::Fon_Current_Pressure);
    if (!pressure) {
        std::cerr << pressure.error().to_string() << '\n';
        return 1;
    }
    std::cout << "pressure = " << *pressure << " bar\n";

    // Typed write; a remote rejection is NOT a transport error.
    auto write = client.datapoints().write(dp::Fon_Event_Label, std::uint8_t{7});
    if (!write) {
        std::cerr << write.error().to_string() << '\n';
    } else if (!write->applied()) {
        for (const auto& error : write->errors) {
            std::cerr << error.datapoint << ": " << error.reason << '\n';
        }
    } else {
        std::cout << "Fon_Event_Label written, readback ok\n";
    }

    (void)client.disconnect();
    return 0;
}
