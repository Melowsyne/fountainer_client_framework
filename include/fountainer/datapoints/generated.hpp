// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// GENERATED — DO NOT EDIT BY HAND.
// Source:  dp_list.def
// Overlay: tools/client_poll_policy.json (client poll policy)
// Regenerate: python3 tools/generate_datapoints.py --dp-list <path>
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include <fountainer/datapoints/datapoint.hpp>

namespace fountainer {

// Changes whenever a datapoint is added, removed, or changes its
// type/access/value range (UI annotations do not count).
inline constexpr std::string_view kDatapointSchemaHash = "8ec39f852fb788c34d2922259aed6b42";
inline constexpr std::size_t kDatapointCount = 107;

inline constexpr double kDatapointUnbounded =
    std::numeric_limits<double>::quiet_NaN();

// Stable order == order in dp_list.def == index into the
// descriptor table. Do NOT use as a persistent key —
// that is what the wire name is for.
enum class DatapointId : std::uint16_t {
    Device_Serial_Number = 0,
    Device_HW_Version = 1,
    Device_SW_Version = 2,
    Device_Build_Version = 3,
    System_Temperature = 4,
    System_Utilization = 5,
    System_Memory_Free = 6,
    System_Min_Memory_Free = 7,
    System_Largest_Free_Block = 8,
    System_Flash_Free = 9,
    System_RSSI = 10,
    System_Uptime = 11,
    System_Min_Stack_Free = 12,
    System_Reconnect_Count = 13,
    System_Reset_Reason = 14,
    System_Power_Mode = 15,
    System_Event_Drops = 16,
    Net_Link_Score = 17,
    Net_Link_State = 18,
    Net_Session_Drops = 19,
    Net_Send_Fail_Count = 20,
    Net_Offline_Seconds = 21,
    Net_Last_Offline_S = 22,
    Ambient_Temperature = 23,
    Ambient_Humidity = 24,
    Fon_Current_Pressure = 25,
    Fon_Sensor_Voltage_mV = 26,
    Fon_Current_State = 27,
    Fon_Relay_Output = 28,
    Fon_Run_Time = 29,
    Fon_Cycles_Total = 30,
    Fon_Remaining_Time = 31,
    Fon_Pressure_Filtered = 32,
    Fon_Pressure_Slope = 33,
    Fon_Demand_State = 34,
    Fon_Anomaly_Score = 35,
    Fon_Event_Duration = 36,
    Fon_Est_Flow_L_Min = 37,
    Fon_Est_Volume_Total = 38,
    Fon_Fault_Code = 39,
    Fon_Starts_Per_Hour = 40,
    Fon_Sensor_Err_Count = 41,
    Fon_Sensor_Noise_mV = 42,
    Fon_Fault_Ack = 43,
    Fon_Event_Label = 44,
    Fon_Pressure_Manual = 45,
    Fon_Pressure_Value = 46,
    Fon_Min_Pressure = 47,
    Fon_Max_Pressure = 48,
    Fon_Alert_High_Pressure = 49,
    Fon_Alert_Low_Pressure = 50,
    Fon_Min_On_Time = 51,
    Fon_Max_On_Time = 52,
    Fon_Dry_Run_Detect_Time = 53,
    Fon_Dry_Run_Min_Rise = 54,
    Fon_Check_Valve_Timeout = 55,
    Fon_Pressure_Drop_Rate = 56,
    Fon_Report_Interval = 57,
    Fon_Sensor_Offset = 58,
    Fon_Sensor_Scale = 59,
    Fon_Sensor_Range_Bar = 60,
    Fon_Min_Off_Time = 61,
    Fon_Filter_Alpha = 62,
    Fon_Stable_Slope = 63,
    Fon_Max_Starts_Per_Hour = 64,
    Fon_Hand_Min_Pressure = 65,
    Fon_Hand_Max_Pressure = 66,
    Fon_Tank_Min_Pressure = 67,
    Fon_Tank_Max_Pressure = 68,
    Fon_Hand_Max_Duration = 69,
    Fon_Tank_Max_Duration = 70,
    Fon_Unknown_Max_Duration = 71,
    Fon_Flow_K_Hand = 72,
    Fon_Flow_K_Tank = 73,
    Network_SSID = 74,
    Network_Password = 75,
    Network_Server = 76,
    Network_Server_Port = 77,
    Network_DHCP = 78,
    Network_IP_Address = 79,
    Network_Subnetmask = 80,
    Network_Gateway = 81,
    Net_PS_Override = 82,
    Backup_DHCP = 83,
    Backup_IP_Address = 84,
    Backup_Subnetmask = 85,
    Backup_Gateway = 86,
    Backup_Server = 87,
    Backup_Server_Port = 88,
    Backup_SSID = 89,
    Backup_Password = 90,
    Log_Enabled = 91,
    Log_Runtime_Level = 92,
    Log_Flash_Level = 93,
    Network_Save = 94,
    Network_Trial_State = 95,
    Log_Next_Seq = 96,
    Log_Dropped = 97,
    Log_Dropped_Flash = 98,
    Log_Prev_Boot_Available = 99,
    Log_Command = 100,
    Pressure_Hist_Next_Seq = 101,
    Pressure_Hist_Overwritten = 102,
    Pressure_Hist_Highwater = 103,
    System_WD_Last_Channel = 104,
    System_WD_Last_Checkpoint = 105,
    System_WD_Reboot_Count = 106,
};

inline constexpr std::array<DatapointDescriptor, kDatapointCount>
    kDatapointDescriptors = {{
        // [  0] Device_Serial_Number
        {0, "Device_Serial_Number", DatapointType::U64, Access::ReadOnly, Persistence::Static, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::OnConnect},
        // [  1] Device_HW_Version
        {1, "Device_HW_Version", DatapointType::Str, Access::ReadOnly, Persistence::Static, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::OnConnect},
        // [  2] Device_SW_Version
        {2, "Device_SW_Version", DatapointType::Str, Access::ReadOnly, Persistence::Static, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::OnConnect},
        // [  3] Device_Build_Version
        {3, "Device_Build_Version", DatapointType::U64, Access::ReadOnly, Persistence::Static, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", "datetime"}, PollClass::OnConnect},
        // [  4] System_Temperature
        {4, "System_Temperature", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"°C", 1, 1.0, "", ""}, PollClass::Status},
        // [  5] System_Utilization
        {5, "System_Utilization", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"%", -1, 0.0, "", ""}, PollClass::Status},
        // [  6] System_Memory_Free
        {6, "System_Memory_Free", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"B", -1, 0.0, "", ""}, PollClass::Status},
        // [  7] System_Min_Memory_Free
        {7, "System_Min_Memory_Free", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"B", -1, 0.0, "", ""}, PollClass::Status},
        // [  8] System_Largest_Free_Block
        {8, "System_Largest_Free_Block", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"B", -1, 0.0, "", ""}, PollClass::Status},
        // [  9] System_Flash_Free
        {9, "System_Flash_Free", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"B", -1, 0.0, "", ""}, PollClass::Status},
        // [ 10] System_RSSI
        {10, "System_RSSI", DatapointType::I8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"dBm", -1, 0.0, "", ""}, PollClass::Status},
        // [ 11] System_Uptime
        {11, "System_Uptime", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"s", -1, 0.0, "", ""}, PollClass::Status},
        // [ 12] System_Min_Stack_Free
        {12, "System_Min_Stack_Free", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"B", -1, 0.0, "", ""}, PollClass::Status},
        // [ 13] System_Reconnect_Count
        {13, "System_Reconnect_Count", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 14] System_Reset_Reason
        {14, "System_Reset_Reason", DatapointType::Enum, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "reset", ""}, PollClass::Status},
        // [ 15] System_Power_Mode
        {15, "System_Power_Mode", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "power", ""}, PollClass::Status},
        // [ 16] System_Event_Drops
        {16, "System_Event_Drops", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 17] Net_Link_Score
        {17, "Net_Link_Score", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 100.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 18] Net_Link_State
        {18, "Net_Link_State", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "linkstate", ""}, PollClass::Status},
        // [ 19] Net_Session_Drops
        {19, "Net_Session_Drops", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 20] Net_Send_Fail_Count
        {20, "Net_Send_Fail_Count", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 21] Net_Offline_Seconds
        {21, "Net_Offline_Seconds", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"s", -1, 0.0, "", ""}, PollClass::Status},
        // [ 22] Net_Last_Offline_S
        {22, "Net_Last_Offline_S", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"s", -1, 0.0, "", ""}, PollClass::Status},
        // [ 23] Ambient_Temperature
        {23, "Ambient_Temperature", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"°C", 1, 0.2, "", ""}, PollClass::Status},
        // [ 24] Ambient_Humidity
        {24, "Ambient_Humidity", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"%rH", 1, 1.0, "", ""}, PollClass::Status},
        // [ 25] Fon_Current_Pressure
        {25, "Fon_Current_Pressure", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"bar", 2, 0.05, "", ""}, PollClass::Realtime},
        // [ 26] Fon_Sensor_Voltage_mV
        {26, "Fon_Sensor_Voltage_mV", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"mV", -1, 0.0, "", ""}, PollClass::Status},
        // [ 27] Fon_Current_State
        {27, "Fon_Current_State", DatapointType::Enum, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "state", ""}, PollClass::Realtime},
        // [ 28] Fon_Relay_Output
        {28, "Fon_Relay_Output", DatapointType::Bool, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "onoff", ""}, PollClass::Realtime},
        // [ 29] Fon_Run_Time
        {29, "Fon_Run_Time", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"s", -1, 0.0, "", ""}, PollClass::Realtime},
        // [ 30] Fon_Cycles_Total
        {30, "Fon_Cycles_Total", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 31] Fon_Remaining_Time
        {31, "Fon_Remaining_Time", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"s", -1, 0.0, "", ""}, PollClass::Realtime},
        // [ 32] Fon_Pressure_Filtered
        {32, "Fon_Pressure_Filtered", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"bar", 2, 0.05, "", ""}, PollClass::Realtime},
        // [ 33] Fon_Pressure_Slope
        {33, "Fon_Pressure_Slope", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"bar/s", 3, 0.01, "", ""}, PollClass::Realtime},
        // [ 34] Fon_Demand_State
        {34, "Fon_Demand_State", DatapointType::Enum, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "demand", ""}, PollClass::Realtime},
        // [ 35] Fon_Anomaly_Score
        {35, "Fon_Anomaly_Score", DatapointType::U16, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 36] Fon_Event_Duration
        {36, "Fon_Event_Duration", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"s", -1, 0.0, "", ""}, PollClass::Status},
        // [ 37] Fon_Est_Flow_L_Min
        {37, "Fon_Est_Flow_L_Min", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"l/min", 1, 0.1, "", ""}, PollClass::Status},
        // [ 38] Fon_Est_Volume_Total
        {38, "Fon_Est_Volume_Total", DatapointType::F32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"l", 1, 0.5, "", ""}, PollClass::Status},
        // [ 39] Fon_Fault_Code
        {39, "Fon_Fault_Code", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "fault", ""}, PollClass::Realtime},
        // [ 40] Fon_Starts_Per_Hour
        {40, "Fon_Starts_Per_Hour", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"/h", -1, 0.0, "", ""}, PollClass::Status},
        // [ 41] Fon_Sensor_Err_Count
        {41, "Fon_Sensor_Err_Count", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 42] Fon_Sensor_Noise_mV
        {42, "Fon_Sensor_Noise_mV", DatapointType::U16, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"mV", -1, 0.0, "", ""}, PollClass::Status},
        // [ 43] Fon_Fault_Ack
        {43, "Fon_Fault_Ack", DatapointType::U8, Access::ReadWrite, Persistence::Volatile, 0, 0.0, 0.0, 1.0, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 44] Fon_Event_Label
        {44, "Fon_Event_Label", DatapointType::U8, Access::ReadWrite, Persistence::Volatile, 0, 0.0, 0.0, 7.0, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 45] Fon_Pressure_Manual
        {45, "Fon_Pressure_Manual", DatapointType::Bool, Access::ReadWrite, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "onoff", ""}, PollClass::Realtime},
        // [ 46] Fon_Pressure_Value
        {46, "Fon_Pressure_Value", DatapointType::F32, Access::ReadWrite, Persistence::Volatile, 0, 0.0, 0.0, 100.0, {"bar", 2, 0.05, "", ""}, PollClass::Realtime},
        // [ 47] Fon_Min_Pressure
        {47, "Fon_Min_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 101, 2.0, 0.0, 10.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 48] Fon_Max_Pressure
        {48, "Fon_Max_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 102, 3.5, 0.0, 10.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 49] Fon_Alert_High_Pressure
        {49, "Fon_Alert_High_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 103, 4.5, 0.0, 12.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 50] Fon_Alert_Low_Pressure
        {50, "Fon_Alert_Low_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 104, 0.3, 0.0, 10.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 51] Fon_Min_On_Time
        {51, "Fon_Min_On_Time", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 105, 30.0, 0.0, 3600.0, {"s", -1, 0.0, "", ""}, PollClass::Config},
        // [ 52] Fon_Max_On_Time
        {52, "Fon_Max_On_Time", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 106, 300.0, 10.0, 65535.0, {"s", -1, 0.0, "", ""}, PollClass::Config},
        // [ 53] Fon_Dry_Run_Detect_Time
        {53, "Fon_Dry_Run_Detect_Time", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 107, 30.0, 1.0, 3600.0, {"s", -1, 0.0, "", ""}, PollClass::Config},
        // [ 54] Fon_Dry_Run_Min_Rise
        {54, "Fon_Dry_Run_Min_Rise", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 108, 100.0, 0.0, 10000.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 55] Fon_Check_Valve_Timeout
        {55, "Fon_Check_Valve_Timeout", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 109, 10.0, 1.0, 3600.0, {"s", -1, 0.0, "", ""}, PollClass::Config},
        // [ 56] Fon_Pressure_Drop_Rate
        {56, "Fon_Pressure_Drop_Rate", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 110, 500.0, 0.0, 65535.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 57] Fon_Report_Interval
        {57, "Fon_Report_Interval", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 111, 10.0, 1.0, 3600.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 58] Fon_Sensor_Offset
        {58, "Fon_Sensor_Offset", DatapointType::I16, Access::ReadWrite, Persistence::Nvs, 112, 0.0, -5000.0, 5000.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 59] Fon_Sensor_Scale
        {59, "Fon_Sensor_Scale", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 113, 1.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 60] Fon_Sensor_Range_Bar
        {60, "Fon_Sensor_Range_Bar", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 114, 34.47, 0.1, 100.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 61] Fon_Min_Off_Time
        {61, "Fon_Min_Off_Time", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 115, 30.0, 0.0, 3600.0, {"s", -1, 0.0, "", ""}, PollClass::Config},
        // [ 62] Fon_Filter_Alpha
        {62, "Fon_Filter_Alpha", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 116, 0.1, 0.01, 1.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 63] Fon_Stable_Slope
        {63, "Fon_Stable_Slope", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 117, 0.02, 0.001, 1.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 64] Fon_Max_Starts_Per_Hour
        {64, "Fon_Max_Starts_Per_Hour", DatapointType::U8, Access::ReadWrite, Persistence::Nvs, 118, 10.0, 1.0, 16.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 65] Fon_Hand_Min_Pressure
        {65, "Fon_Hand_Min_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 119, 1.7, 0.0, 100.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 66] Fon_Hand_Max_Pressure
        {66, "Fon_Hand_Max_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 120, 2.0, 0.0, 100.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 67] Fon_Tank_Min_Pressure
        {67, "Fon_Tank_Min_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 121, 1.2, 0.0, 100.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 68] Fon_Tank_Max_Pressure
        {68, "Fon_Tank_Max_Pressure", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 122, 1.6, 0.0, 100.0, {"bar", 2, 0.0, "", ""}, PollClass::Config},
        // [ 69] Fon_Hand_Max_Duration
        {69, "Fon_Hand_Max_Duration", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 123, 20.0, 1.0, 1440.0, {"min", -1, 0.0, "", ""}, PollClass::Config},
        // [ 70] Fon_Tank_Max_Duration
        {70, "Fon_Tank_Max_Duration", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 124, 12.0, 1.0, 1440.0, {"min", -1, 0.0, "", ""}, PollClass::Config},
        // [ 71] Fon_Unknown_Max_Duration
        {71, "Fon_Unknown_Max_Duration", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 125, 10.0, 1.0, 1440.0, {"min", -1, 0.0, "", ""}, PollClass::Config},
        // [ 72] Fon_Flow_K_Hand
        {72, "Fon_Flow_K_Hand", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 126, 0.0, 0.0, 1000.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 73] Fon_Flow_K_Tank
        {73, "Fon_Flow_K_Tank", DatapointType::F32, Access::ReadWrite, Persistence::Nvs, 127, 0.0, 0.0, 1000.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 74] Network_SSID
        {74, "Network_SSID", DatapointType::Str, Access::ReadWrite, Persistence::Nvs, 201, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 75] Network_Password
        {75, "Network_Password", DatapointType::Str, Access::ReadWrite, Persistence::Nvs, 202, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Disabled},
        // [ 76] Network_Server
        {76, "Network_Server", DatapointType::Str, Access::ReadWrite, Persistence::Nvs, 203, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 77] Network_Server_Port
        {77, "Network_Server_Port", DatapointType::U16, Access::ReadWrite, Persistence::Nvs, 204, 8443.0, 1.0, 65535.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 78] Network_DHCP
        {78, "Network_DHCP", DatapointType::Bool, Access::ReadWrite, Persistence::Nvs, 205, 1.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "dhcp", ""}, PollClass::Config},
        // [ 79] Network_IP_Address
        {79, "Network_IP_Address", DatapointType::Str, Access::ReadWrite, Persistence::Nvs, 206, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 80] Network_Subnetmask
        {80, "Network_Subnetmask", DatapointType::Str, Access::ReadWrite, Persistence::Nvs, 207, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 81] Network_Gateway
        {81, "Network_Gateway", DatapointType::Str, Access::ReadWrite, Persistence::Nvs, 208, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 82] Net_PS_Override
        {82, "Net_PS_Override", DatapointType::U8, Access::ReadWrite, Persistence::Nvs, 210, 0.0, 0.0, 2.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 83] Backup_DHCP
        {83, "Backup_DHCP", DatapointType::Bool, Access::ReadOnly, Persistence::Nvs, 301, 1.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "dhcp", ""}, PollClass::Config},
        // [ 84] Backup_IP_Address
        {84, "Backup_IP_Address", DatapointType::Str, Access::ReadOnly, Persistence::Nvs, 302, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 85] Backup_Subnetmask
        {85, "Backup_Subnetmask", DatapointType::Str, Access::ReadOnly, Persistence::Nvs, 303, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 86] Backup_Gateway
        {86, "Backup_Gateway", DatapointType::Str, Access::ReadOnly, Persistence::Nvs, 304, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 87] Backup_Server
        {87, "Backup_Server", DatapointType::Str, Access::ReadOnly, Persistence::Nvs, 305, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 88] Backup_Server_Port
        {88, "Backup_Server_Port", DatapointType::U16, Access::ReadOnly, Persistence::Nvs, 306, 8443.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 89] Backup_SSID
        {89, "Backup_SSID", DatapointType::Str, Access::ReadOnly, Persistence::Nvs, 307, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 90] Backup_Password
        {90, "Backup_Password", DatapointType::Str, Access::ReadOnly, Persistence::Nvs, 308, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Disabled},
        // [ 91] Log_Enabled
        {91, "Log_Enabled", DatapointType::Bool, Access::ReadWrite, Persistence::Nvs, 401, 1.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 92] Log_Runtime_Level
        {92, "Log_Runtime_Level", DatapointType::U8, Access::ReadWrite, Persistence::Nvs, 402, 3.0, 0.0, 5.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 93] Log_Flash_Level
        {93, "Log_Flash_Level", DatapointType::U8, Access::ReadWrite, Persistence::Nvs, 403, 2.0, 0.0, 5.0, {"", -1, 0.0, "", ""}, PollClass::Config},
        // [ 94] Network_Save
        {94, "Network_Save", DatapointType::U8, Access::ReadWrite, Persistence::Volatile, 0, 0.0, 0.0, 4.0, {"", -1, 0.0, "", ""}, PollClass::Disabled},
        // [ 95] Network_Trial_State
        {95, "Network_Trial_State", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "trial", ""}, PollClass::Config},
        // [ 96] Log_Next_Seq
        {96, "Log_Next_Seq", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 97] Log_Dropped
        {97, "Log_Dropped", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 98] Log_Dropped_Flash
        {98, "Log_Dropped_Flash", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [ 99] Log_Prev_Boot_Available
        {99, "Log_Prev_Boot_Available", DatapointType::Bool, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [100] Log_Command
        {100, "Log_Command", DatapointType::U8, Access::ReadWrite, Persistence::Volatile, 0, 0.0, 0.0, 3.0, {"", -1, 0.0, "", ""}, PollClass::Disabled},
        // [101] Pressure_Hist_Next_Seq
        {101, "Pressure_Hist_Next_Seq", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [102] Pressure_Hist_Overwritten
        {102, "Pressure_Hist_Overwritten", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [103] Pressure_Hist_Highwater
        {103, "Pressure_Hist_Highwater", DatapointType::U32, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [104] System_WD_Last_Channel
        {104, "System_WD_Last_Channel", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [105] System_WD_Last_Checkpoint
        {105, "System_WD_Last_Checkpoint", DatapointType::U16, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
        // [106] System_WD_Reboot_Count
        {106, "System_WD_Reboot_Count", DatapointType::U8, Access::ReadOnly, Persistence::Volatile, 0, 0.0, kDatapointUnbounded, kDatapointUnbounded, {"", -1, 0.0, "", ""}, PollClass::Status},
    }};

template <typename T, Access A, Persistence P>
constexpr const DatapointDescriptor&
Datapoint<T, A, P>::descriptor() const noexcept
{
    return kDatapointDescriptors[index];
}

// Typed constants — the default API (design concept §10.1).
//   client.datapoints().read(dp::Fon_Current_Pressure)  -> Result<float>
namespace dp {

inline constexpr Datapoint<std::uint64_t, Access::ReadOnly, Persistence::Static>
    Device_Serial_Number{0, "Device_Serial_Number"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Static>
    Device_HW_Version{1, "Device_HW_Version"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Static>
    Device_SW_Version{2, "Device_SW_Version"};
inline constexpr Datapoint<std::uint64_t, Access::ReadOnly, Persistence::Static>
    Device_Build_Version{3, "Device_Build_Version"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    System_Temperature{4, "System_Temperature"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    System_Utilization{5, "System_Utilization"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Memory_Free{6, "System_Memory_Free"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Min_Memory_Free{7, "System_Min_Memory_Free"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Largest_Free_Block{8, "System_Largest_Free_Block"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Flash_Free{9, "System_Flash_Free"};
inline constexpr Datapoint<std::int8_t, Access::ReadOnly, Persistence::Volatile>
    System_RSSI{10, "System_RSSI"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Uptime{11, "System_Uptime"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Min_Stack_Free{12, "System_Min_Stack_Free"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Reconnect_Count{13, "System_Reconnect_Count"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    System_Reset_Reason{14, "System_Reset_Reason"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    System_Power_Mode{15, "System_Power_Mode"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    System_Event_Drops{16, "System_Event_Drops"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Net_Link_Score{17, "Net_Link_Score"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Net_Link_State{18, "Net_Link_State"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Net_Session_Drops{19, "Net_Session_Drops"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Net_Send_Fail_Count{20, "Net_Send_Fail_Count"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Net_Offline_Seconds{21, "Net_Offline_Seconds"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Net_Last_Offline_S{22, "Net_Last_Offline_S"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Ambient_Temperature{23, "Ambient_Temperature"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Ambient_Humidity{24, "Ambient_Humidity"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Fon_Current_Pressure{25, "Fon_Current_Pressure"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Sensor_Voltage_mV{26, "Fon_Sensor_Voltage_mV"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Current_State{27, "Fon_Current_State"};
inline constexpr Datapoint<bool, Access::ReadOnly, Persistence::Volatile>
    Fon_Relay_Output{28, "Fon_Relay_Output"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Run_Time{29, "Fon_Run_Time"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Cycles_Total{30, "Fon_Cycles_Total"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Remaining_Time{31, "Fon_Remaining_Time"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Fon_Pressure_Filtered{32, "Fon_Pressure_Filtered"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Fon_Pressure_Slope{33, "Fon_Pressure_Slope"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Demand_State{34, "Fon_Demand_State"};
inline constexpr Datapoint<std::uint16_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Anomaly_Score{35, "Fon_Anomaly_Score"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Event_Duration{36, "Fon_Event_Duration"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Fon_Est_Flow_L_Min{37, "Fon_Est_Flow_L_Min"};
inline constexpr Datapoint<float, Access::ReadOnly, Persistence::Volatile>
    Fon_Est_Volume_Total{38, "Fon_Est_Volume_Total"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Fault_Code{39, "Fon_Fault_Code"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Starts_Per_Hour{40, "Fon_Starts_Per_Hour"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Sensor_Err_Count{41, "Fon_Sensor_Err_Count"};
inline constexpr Datapoint<std::uint16_t, Access::ReadOnly, Persistence::Volatile>
    Fon_Sensor_Noise_mV{42, "Fon_Sensor_Noise_mV"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Volatile>
    Fon_Fault_Ack{43, "Fon_Fault_Ack"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Volatile>
    Fon_Event_Label{44, "Fon_Event_Label"};
inline constexpr Datapoint<bool, Access::ReadWrite, Persistence::Volatile>
    Fon_Pressure_Manual{45, "Fon_Pressure_Manual"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Volatile>
    Fon_Pressure_Value{46, "Fon_Pressure_Value"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Min_Pressure{47, "Fon_Min_Pressure"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Max_Pressure{48, "Fon_Max_Pressure"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Alert_High_Pressure{49, "Fon_Alert_High_Pressure"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Alert_Low_Pressure{50, "Fon_Alert_Low_Pressure"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Min_On_Time{51, "Fon_Min_On_Time"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Max_On_Time{52, "Fon_Max_On_Time"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Dry_Run_Detect_Time{53, "Fon_Dry_Run_Detect_Time"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Dry_Run_Min_Rise{54, "Fon_Dry_Run_Min_Rise"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Check_Valve_Timeout{55, "Fon_Check_Valve_Timeout"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Pressure_Drop_Rate{56, "Fon_Pressure_Drop_Rate"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Report_Interval{57, "Fon_Report_Interval"};
inline constexpr Datapoint<std::int16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Sensor_Offset{58, "Fon_Sensor_Offset"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Sensor_Scale{59, "Fon_Sensor_Scale"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Sensor_Range_Bar{60, "Fon_Sensor_Range_Bar"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Min_Off_Time{61, "Fon_Min_Off_Time"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Filter_Alpha{62, "Fon_Filter_Alpha"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Stable_Slope{63, "Fon_Stable_Slope"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Max_Starts_Per_Hour{64, "Fon_Max_Starts_Per_Hour"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Hand_Min_Pressure{65, "Fon_Hand_Min_Pressure"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Hand_Max_Pressure{66, "Fon_Hand_Max_Pressure"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Tank_Min_Pressure{67, "Fon_Tank_Min_Pressure"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Tank_Max_Pressure{68, "Fon_Tank_Max_Pressure"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Hand_Max_Duration{69, "Fon_Hand_Max_Duration"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Tank_Max_Duration{70, "Fon_Tank_Max_Duration"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Fon_Unknown_Max_Duration{71, "Fon_Unknown_Max_Duration"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Flow_K_Hand{72, "Fon_Flow_K_Hand"};
inline constexpr Datapoint<float, Access::ReadWrite, Persistence::Nvs>
    Fon_Flow_K_Tank{73, "Fon_Flow_K_Tank"};
inline constexpr Datapoint<std::string, Access::ReadWrite, Persistence::Nvs>
    Network_SSID{74, "Network_SSID"};
inline constexpr Datapoint<std::string, Access::ReadWrite, Persistence::Nvs>
    Network_Password{75, "Network_Password"};
inline constexpr Datapoint<std::string, Access::ReadWrite, Persistence::Nvs>
    Network_Server{76, "Network_Server"};
inline constexpr Datapoint<std::uint16_t, Access::ReadWrite, Persistence::Nvs>
    Network_Server_Port{77, "Network_Server_Port"};
inline constexpr Datapoint<bool, Access::ReadWrite, Persistence::Nvs>
    Network_DHCP{78, "Network_DHCP"};
inline constexpr Datapoint<std::string, Access::ReadWrite, Persistence::Nvs>
    Network_IP_Address{79, "Network_IP_Address"};
inline constexpr Datapoint<std::string, Access::ReadWrite, Persistence::Nvs>
    Network_Subnetmask{80, "Network_Subnetmask"};
inline constexpr Datapoint<std::string, Access::ReadWrite, Persistence::Nvs>
    Network_Gateway{81, "Network_Gateway"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Nvs>
    Net_PS_Override{82, "Net_PS_Override"};
inline constexpr Datapoint<bool, Access::ReadOnly, Persistence::Nvs>
    Backup_DHCP{83, "Backup_DHCP"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Nvs>
    Backup_IP_Address{84, "Backup_IP_Address"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Nvs>
    Backup_Subnetmask{85, "Backup_Subnetmask"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Nvs>
    Backup_Gateway{86, "Backup_Gateway"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Nvs>
    Backup_Server{87, "Backup_Server"};
inline constexpr Datapoint<std::uint16_t, Access::ReadOnly, Persistence::Nvs>
    Backup_Server_Port{88, "Backup_Server_Port"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Nvs>
    Backup_SSID{89, "Backup_SSID"};
inline constexpr Datapoint<std::string, Access::ReadOnly, Persistence::Nvs>
    Backup_Password{90, "Backup_Password"};
inline constexpr Datapoint<bool, Access::ReadWrite, Persistence::Nvs>
    Log_Enabled{91, "Log_Enabled"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Nvs>
    Log_Runtime_Level{92, "Log_Runtime_Level"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Nvs>
    Log_Flash_Level{93, "Log_Flash_Level"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Volatile>
    Network_Save{94, "Network_Save"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    Network_Trial_State{95, "Network_Trial_State"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Log_Next_Seq{96, "Log_Next_Seq"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Log_Dropped{97, "Log_Dropped"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Log_Dropped_Flash{98, "Log_Dropped_Flash"};
inline constexpr Datapoint<bool, Access::ReadOnly, Persistence::Volatile>
    Log_Prev_Boot_Available{99, "Log_Prev_Boot_Available"};
inline constexpr Datapoint<std::uint8_t, Access::ReadWrite, Persistence::Volatile>
    Log_Command{100, "Log_Command"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Pressure_Hist_Next_Seq{101, "Pressure_Hist_Next_Seq"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Pressure_Hist_Overwritten{102, "Pressure_Hist_Overwritten"};
inline constexpr Datapoint<std::uint32_t, Access::ReadOnly, Persistence::Volatile>
    Pressure_Hist_Highwater{103, "Pressure_Hist_Highwater"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    System_WD_Last_Channel{104, "System_WD_Last_Channel"};
inline constexpr Datapoint<std::uint16_t, Access::ReadOnly, Persistence::Volatile>
    System_WD_Last_Checkpoint{105, "System_WD_Last_Checkpoint"};
inline constexpr Datapoint<std::uint8_t, Access::ReadOnly, Persistence::Volatile>
    System_WD_Reboot_Count{106, "System_WD_Reboot_Count"};

}  // namespace dp

}  // namespace fountainer
