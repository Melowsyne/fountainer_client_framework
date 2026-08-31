// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/catalog.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>

namespace fountainer {

std::string_view to_string(DatapointType type) noexcept
{
    switch (type) {
    case DatapointType::Bool: return "BOOL";
    case DatapointType::U8:   return "U8";
    case DatapointType::U16:  return "U16";
    case DatapointType::U32:  return "U32";
    case DatapointType::U64:  return "U64";
    case DatapointType::I8:   return "I8";
    case DatapointType::I16:  return "I16";
    case DatapointType::I32:  return "I32";
    case DatapointType::F32:  return "F32";
    case DatapointType::Enum: return "ENUM";
    case DatapointType::Str:  return "STR";
    }
    return "?";
}

std::string_view to_string(Access access) noexcept
{
    switch (access) {
    case Access::ReadOnly:  return "RO";
    case Access::ReadWrite: return "RW";
    case Access::WriteOnly: return "WO";
    }
    return "?";
}

std::string_view to_string(Persistence persistence) noexcept
{
    switch (persistence) {
    case Persistence::Volatile: return "VOLATILE";
    case Persistence::Nvs:      return "NVS";
    case Persistence::Static:   return "STATIC";
    }
    return "?";
}

std::string_view to_string(PollClass poll_class) noexcept
{
    switch (poll_class) {
    case PollClass::Realtime:  return "Realtime";
    case PollClass::Status:    return "Status";
    case PollClass::Config:    return "Config";
    case PollClass::OnConnect: return "OnConnect";
    case PollClass::Disabled:  return "Disabled";
    }
    return "?";
}

namespace catalog {
namespace {

// Name index built once — dp_read/dp_report look up as many as 107 names
// per frame; a linear search would be noticeable there.
const std::unordered_map<std::string_view, const DatapointDescriptor*>& index()
{
    static const auto* map = [] {
        auto* built =
            new std::unordered_map<std::string_view, const DatapointDescriptor*>();
        built->reserve(kDatapointDescriptors.size() * 2);
        for (const auto& descriptor : kDatapointDescriptors) {
            built->emplace(descriptor.name, &descriptor);
        }
        return built;
    }();
    return *map;
}

std::vector<std::string_view> collect(bool (*predicate)(const DatapointDescriptor&))
{
    std::vector<std::string_view> out;
    out.reserve(kDatapointDescriptors.size());
    for (const auto& descriptor : kDatapointDescriptors) {
        if (predicate(descriptor)) out.push_back(descriptor.name);
    }
    return out;
}

}  // namespace

const DatapointDescriptor* find(std::string_view name) noexcept
{
    const auto& map = index();
    const auto it = map.find(name);
    return it == map.end() ? nullptr : it->second;
}

std::vector<std::string_view> all_names()
{
    return collect([](const DatapointDescriptor&) { return true; });
}

std::vector<std::string_view> names_with_access(Access access)
{
    std::vector<std::string_view> out;
    for (const auto& d : kDatapointDescriptors) {
        if (d.access == access) out.push_back(d.name);
    }
    return out;
}

std::vector<std::string_view> names_with_persistence(Persistence persistence)
{
    std::vector<std::string_view> out;
    for (const auto& d : kDatapointDescriptors) {
        if (d.persistence == persistence) out.push_back(d.name);
    }
    return out;
}

std::vector<std::string_view> names_with_poll_class(PollClass poll_class)
{
    std::vector<std::string_view> out;
    for (const auto& d : kDatapointDescriptors) {
        if (d.poll_class == poll_class) out.push_back(d.name);
    }
    return out;
}

}  // namespace catalog
}  // namespace fountainer
