# Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
# SPDX-License-Identifier: MIT
#
# fountainer — C++20 framework for Fountain protocol access to
# ESP32-S3 devices (locally via WSS/mTLS, later via backend).
# Design docs (concept / refactoring plan) are kept outside this repository.
#
#   fountainer_core    protocol + datapoints, WITHOUT Asio/Beast — reusable in
#                      the backend over accepted connections
#   fountainer_client  Beast WSS transport + client facade (own IO thread)
#   fountainer-cli     command-line tool on top of the public API
cmake_minimum_required(VERSION 3.20)

project(
    fountainer
    VERSION 0.2.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0167 NEW)   # FindBoost module is deprecated -> BoostConfig
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# --- Core: protocol, datapoints, domain services — asio-free -----------------
add_library(
    fountainer_core STATIC

    src/error.cpp
    src/events.cpp
    src/security.cpp
    src/commands.cpp
    src/logs.cpp
    src/maintenance.cpp

    src/datapoints/catalog.cpp
    src/datapoints/codec.cpp
    src/datapoints/write_set.cpp
    src/datapoints/manager.cpp
    src/datapoints/poller.cpp
    src/datapoints/restore_guard.cpp
    src/datapoints/constraints.cpp

    src/protocol/canonical.cpp
    src/protocol/auth.cpp
    src/protocol/envelope.cpp
    src/protocol/dispatcher.cpp
    src/protocol/session.cpp

    src/logging/logger.cpp
)

target_include_directories(fountainer_core PUBLIC include)
target_link_libraries(
    fountainer_core
    PUBLIC
        OpenSSL::Crypto
        Threads::Threads
)

# --- Client: Beast transport + facade ----------------------------------------
add_library(
    fountainer_client STATIC

    src/client.cpp
    src/config.cpp
    src/transport/wss_transport.cpp
)

target_link_libraries(
    fountainer_client
    PUBLIC
        fountainer_core
        Boost::system
        OpenSSL::SSL
)

add_library(fountainer::core ALIAS fountainer_core)
add_library(fountainer::client ALIAS fountainer_client)

# --- Tool --------------------------------------------------------------------
add_executable(fountainer-cli apps/fountainer-cli/main.cpp)
target_link_libraries(fountainer-cli PRIVATE fountainer_client)

# --- Examples ----------------------------------------------------------------
option(FOUNTAINER_BUILD_EXAMPLES "Build example programs" ON)
if(FOUNTAINER_BUILD_EXAMPLES)
    foreach(example simple_client polling_client config_editor)
        add_executable(example_${example} examples/${example}.cpp)
        target_link_libraries(example_${example} PRIVATE fountainer_client)
    endforeach()

    # Annotated tutorial example (class DeviceExample): the complete API
    # from connect() to disconnect() against a real device.
    add_executable(example_class src/Example.cpp)
    target_link_libraries(example_class PRIVATE fountainer_client)
endif()

enable_testing()
add_subdirectory(test)
