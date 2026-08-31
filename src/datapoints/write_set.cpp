// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/write_set.hpp"

namespace fountainer {

Status DatapointWriteSet::set(std::string_view name, DatapointValue value)
{
    const DatapointDescriptor* descriptor = catalog::find(name);
    if (descriptor == nullptr) {
        return fail(validation_error(ErrorCode::UnknownDatapoint,
                                     "unknown datapoint '" + std::string(name) + "'",
                                     std::string(name)));
    }
    if (!descriptor->writable()) {
        return fail(validation_error(ErrorCode::ReadOnlyDatapoint,
                                     std::string(name) + " is read-only",
                                     std::string(name)));
    }
    if (value.index() != value_index_for(descriptor->type)) {
        return fail(validation_error(ErrorCode::TypeMismatch,
                                     std::string(name) + " expects " +
                                         std::string(to_string(descriptor->type)),
                                     std::string(name)));
    }
    values_.insert_or_assign(std::string(name), std::move(value));
    return ok();
}

Status DatapointWriteSet::validate() const
{
    if (values_.empty()) {
        return fail(validation_error(ErrorCode::EmptySelection,
                                     "write set contains no datapoints"));
    }
    for (const auto& [name, value] : values_) {
        const DatapointDescriptor* descriptor = catalog::find(name);
        if (descriptor == nullptr) {
            return fail(validation_error(ErrorCode::UnknownDatapoint,
                                         "unknown datapoint '" + name + "'", name));
        }
        if (auto status = fountainer::validate(*descriptor, value); !status) {
            return status;
        }
    }
    return ok();
}

}  // namespace fountainer
