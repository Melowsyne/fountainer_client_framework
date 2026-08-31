// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Catalog + codec: the generated catalog must mirror the firmware contract
// exactly (107 datapoints, U64 as hex string, STR < 64, ENUM as U8).
#include <catch2/catch_test_macros.hpp>

#include <fountainer/datapoints/catalog.hpp>
#include <fountainer/datapoints/codec.hpp>
#include <fountainer/datapoints/write_set.hpp>
#include <fountainer/datapoints/constraints.hpp>

using namespace fountainer;

TEST_CASE("catalog matches the firmware inventory")
{
    CHECK(kDatapointCount == 107);

    std::size_t read_only = 0, read_write = 0, volatile_count = 0, nvs = 0,
                static_count = 0;
    for (const auto& d : catalog::all()) {
        if (d.access == Access::ReadOnly) read_only++;
        if (d.access == Access::ReadWrite) read_write++;
        if (d.persistence == Persistence::Volatile) volatile_count++;
        if (d.persistence == Persistence::Nvs) nvs++;
        if (d.persistence == Persistence::Static) static_count++;
    }
    // Counts from the design concept §2.2 (dp_list.def).
    CHECK(read_only == 59);
    CHECK(read_write == 45);
    CHECK(volatile_count == 56);
    CHECK(nvs == 47);
    CHECK(static_count == 4);
}

TEST_CASE("catalog lookup")
{
    const auto* pressure = catalog::find("Fon_Current_Pressure");
    REQUIRE(pressure != nullptr);
    CHECK(pressure->type == DatapointType::F32);
    CHECK(pressure->access == Access::ReadOnly);
    CHECK(pressure->metadata.unit == "bar");

    CHECK(catalog::find("Does_Not_Exist") == nullptr);

    // Typed constant and table point to the same descriptor.
    CHECK(&catalog::at(dp::Fon_Min_Pressure) == catalog::find("Fon_Min_Pressure"));
    CHECK(dp::Fon_Min_Pressure.descriptor().min == 0.0);
    CHECK(dp::Fon_Min_Pressure.descriptor().max == 10.0);
}

TEST_CASE("write access is a compile-time property")
{
    STATIC_CHECK(WritableDatapoint<decltype(dp::Fon_Min_Pressure)>);
    STATIC_CHECK(WritableDatapoint<decltype(dp::Fon_Event_Label)>);
    STATIC_CHECK(!WritableDatapoint<decltype(dp::Device_SW_Version)>);
    STATIC_CHECK(!WritableDatapoint<decltype(dp::Fon_Current_Pressure)>);
}

TEST_CASE("codec: U64 travels as hex string")
{
    const auto& serial = catalog::at(dp::Device_Serial_Number);

    auto value = value_from_json(serial, nlohmann::json("000001C0C01FA82A"));
    REQUIRE(value);
    CHECK(std::get<std::uint64_t>(*value) == 0x000001C0C01FA82AULL);

    CHECK(value_to_json(serial, *value) == nlohmann::json("000001C0C01FA82A"));

    // A number instead of a string is a type error.
    CHECK_FALSE(value_from_json(serial, nlohmann::json(42)));
    CHECK_FALSE(value_from_json(serial, nlohmann::json("hello")));
}

TEST_CASE("codec: bool and enum")
{
    const auto& relay = catalog::at(dp::Fon_Relay_Output);
    auto on = value_from_json(relay, nlohmann::json(true));
    REQUIRE(on);
    CHECK(std::get<bool>(*on) == true);
    CHECK_FALSE(value_from_json(relay, nlohmann::json(1)));   // no 0/1

    const auto& state = catalog::at(dp::Fon_Current_State);
    auto enum_value = value_from_json(state, nlohmann::json(3));
    REQUIRE(enum_value);
    CHECK(std::get<std::uint8_t>(*enum_value) == 3);
}

TEST_CASE("codec: integers reject silent truncation")
{
    const auto& label = catalog::at(dp::Fon_Event_Label);   // U8
    CHECK_FALSE(value_from_json(label, nlohmann::json(300)));
    auto ok_value = value_from_json(label, nlohmann::json(7));
    REQUIRE(ok_value);
    CHECK(std::get<std::uint8_t>(*ok_value) == 7);
}

TEST_CASE("validate: range and string length")
{
    const auto& min_pressure = catalog::at(dp::Fon_Min_Pressure);
    CHECK(validate(min_pressure, DatapointValue{5.0f}));
    CHECK_FALSE(validate(min_pressure, DatapointValue{99.0f}));
    CHECK_FALSE(validate(min_pressure, DatapointValue{std::uint8_t{1}}));

    const auto& ssid = catalog::at(dp::Network_SSID);
    CHECK(validate(ssid, DatapointValue{std::string(63, 'x')}));
    auto too_long = validate(ssid, DatapointValue{std::string(64, 'x')});
    REQUIRE_FALSE(too_long);
    CHECK(too_long.error().code == ErrorCode::ValueTooLong);
}

TEST_CASE("write set enforces access and type")
{
    DatapointWriteSet changes;

    CHECK(changes.set("Fon_Event_Label", DatapointValue{std::uint8_t{7}}));
    CHECK_FALSE(changes.set("Fon_Current_Pressure", DatapointValue{1.0f}));  // RO
    CHECK_FALSE(changes.set("Nope", DatapointValue{1.0f}));
    CHECK_FALSE(changes.set("Fon_Min_Pressure", DatapointValue{std::string("x")}));

    changes.set(dp::Fon_Min_Pressure, 2.5f);
    CHECK(changes.size() == 2);
    CHECK(changes.validate());

    changes.set(dp::Fon_Min_Pressure, 99.0f);   // out of range
    CHECK_FALSE(changes.validate());
}

TEST_CASE("cross-field constraints mirror dp_constraints_ok")
{
    // Mirror of the firmware rules (datapoints.c §9).
    DatapointWriteSet broken;
    broken.set(dp::Fon_Min_Pressure, 3.0f);
    broken.set(dp::Fon_Max_Pressure, 2.0f);
    auto violation = check_cross_constraints(broken, nullptr);
    REQUIRE_FALSE(violation.has_value());
    CHECK(violation.error().code == ErrorCode::ConstraintViolation);
    CHECK(violation.error().datapoint == "Fon_Max_Pressure");

    DatapointWriteSet chain;
    chain.set(dp::Fon_Alert_Low_Pressure, 0.3f);
    chain.set(dp::Fon_Min_Pressure, 2.0f);
    chain.set(dp::Fon_Max_Pressure, 3.5f);
    chain.set(dp::Fon_Alert_High_Pressure, 4.5f);
    CHECK(check_cross_constraints(chain, nullptr));

    // Rule against the FALLBACK (cache): Max_On_Time does not come from the set.
    DatapointWriteSet timing;
    timing.set(dp::Fon_Min_On_Time, std::uint16_t{500});
    auto vs_cache = check_cross_constraints(
        timing, [](std::string_view name) -> std::optional<double> {
            if (name == "Fon_Max_On_Time") return 300.0;
            return std::nullopt;
        });
    REQUIRE_FALSE(vs_cache.has_value());
    CHECK(vs_cache.error().datapoint == "Fon_Min_On_Time");

    // Unknown operands: no rule applicable -> ok (the firmware checks).
    DatapointWriteSet lonely;
    lonely.set(dp::Fon_Min_On_Time, std::uint16_t{500});
    CHECK(check_cross_constraints(lonely, nullptr));
}
