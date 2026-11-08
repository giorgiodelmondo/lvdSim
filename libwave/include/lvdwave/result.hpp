// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace lvd {

/// Minimal value-or-error type (C++20 has no std::expected). The error is a human readable message.
template <typename T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}  // NOLINT: implicit by design
    static Result failure(std::string message) {
        Result r{Tag{}};
        r.v_.template emplace<1>(std::move(message));
        return r;
    }

    bool ok() const noexcept { return v_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }
    T& value() & { return std::get<0>(v_); }
    const T& value() const& { return std::get<0>(v_); }
    T&& value() && { return std::get<0>(std::move(v_)); }
    const std::string& error() const { return std::get<1>(v_); }

private:
    struct Tag {};
    explicit Result(Tag) : v_(std::in_place_index<1>) {}
    std::variant<T, std::string> v_;
};

}  // namespace lvd
