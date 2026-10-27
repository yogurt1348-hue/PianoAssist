#pragma once

#include <optional>
#include <string>
#include <utility>

namespace pianoassist
{

/** A value or a human-readable error. Domain code returns this instead of
    throwing, so untrusted input (MIDI files, settings) fails predictably. */
template <typename T>
class Result
{
public:
    static Result ok (T value) { return Result (std::move (value), {}); }
    static Result fail (std::string message) { return Result (std::nullopt, std::move (message)); }

    bool isOk() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return isOk(); }

    const T& value() const& { return *value_; }
    T& value() & { return *value_; }
    T&& value() && { return std::move (*value_); }

    const std::string& error() const noexcept { return error_; }

private:
    Result (std::optional<T> value, std::string error)
        : value_ (std::move (value)), error_ (std::move (error)) {}

    std::optional<T> value_;
    std::string error_;
};

} // namespace pianoassist
