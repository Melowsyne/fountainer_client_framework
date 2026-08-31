// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Result<T> — a small expected implementation for C++20 (design concept §19.1).
// On a later switch to C++23 the body can be replaced by std::expected
// without changing the call sites.
#pragma once

#include <cassert>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

#include <fountainer/error.hpp>

namespace fountainer {

// Explicitly "this is an error" — avoids ambiguity when T itself
// would be constructible from an Error.
struct unexpected_t {
    Error error;
};

inline unexpected_t fail(Error error) { return unexpected_t{std::move(error)}; }

inline unexpected_t fail(ErrorDomain domain, ErrorCode code, std::string message)
{
    return unexpected_t{make_error(domain, code, std::move(message))};
}

template <typename T>
class Result {
public:
    using value_type = T;

    Result(T value) : store_(std::in_place_index<0>, std::move(value)) {}
    Result(unexpected_t error)
        : store_(std::in_place_index<1>, std::move(error.error))
    {
    }

    // Errors may move between Result types without loss:
    //   Result<Foo> f = ...; if (!f) return Result<Bar>{fail(f.error())};
    template <typename U>
    Result(const Result<U>& other, std::enable_if_t<!std::is_same_v<T, U>, int> = 0)
        : store_(std::in_place_index<1>, other.error())
    {
        assert(!other.has_value() && "only errors are convertible");
    }

    [[nodiscard]] bool has_value() const noexcept { return store_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    T& value() & { return std::get<0>(store_); }
    const T& value() const& { return std::get<0>(store_); }
    T&& value() && { return std::get<0>(std::move(store_)); }

    T* operator->() { return &std::get<0>(store_); }
    const T* operator->() const { return &std::get<0>(store_); }
    T& operator*() & { return std::get<0>(store_); }
    const T& operator*() const& { return std::get<0>(store_); }
    T&& operator*() && { return std::get<0>(std::move(store_)); }

    const Error& error() const& { return std::get<1>(store_); }
    Error&& error() && { return std::get<1>(std::move(store_)); }

    // Convenient default when the application does not care about the error.
    T value_or(T fallback) const&
    {
        return has_value() ? std::get<0>(store_) : std::move(fallback);
    }

private:
    std::variant<T, Error> store_;
};

// Specialisation without a payload value — "success" or "error".
template <>
class Result<void> {
public:
    using value_type = void;

    Result() = default;
    Result(unexpected_t error) : error_(std::move(error.error)), ok_(false) {}

    template <typename U>
    Result(const Result<U>& other) : error_(other.error()), ok_(false)
    {
        assert(!other.has_value() && "only errors are convertible");
    }

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    void value() const {}

    const Error& error() const& { return error_; }
    Error&& error() && { return std::move(error_); }

private:
    Error error_{};
    bool ok_ = true;
};

using Status = Result<void>;

inline Status ok() { return Status{}; }

}  // namespace fountainer
