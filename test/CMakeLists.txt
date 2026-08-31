# Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
# SPDX-License-Identifier: MIT
#
# Unit tests (Catch2) against fountainer_core/fountainer_client — network and
# device tests run separately (tools/, test/hardware later).
find_package(Catch2 3 REQUIRED)

add_executable(
    unit_tests

    test_config.cpp
    test_canonical.cpp
    test_auth.cpp
    test_envelope.cpp
    test_datapoints.cpp
    test_dispatcher.cpp
    test_controller_session.cpp
    test_manager_poller.cpp
    test_logs_commands.cpp
    test_api_review.cpp
)

target_link_libraries(
    unit_tests
    PRIVATE
        fountainer_client
        Catch2::Catch2WithMain
)

add_test(NAME unit_tests COMMAND unit_tests)

# Hardware test against a REAL device (safe matrix + limits, design concept
# §25/§26). Deliberately NO ctest entry — invoke by hand:
#   /app/build/test/hardware_test /app/config/client.fnt-000001.json
add_executable(hardware_test hardware/Test.cpp)
target_link_libraries(hardware_test PRIVATE fountainer_client)
