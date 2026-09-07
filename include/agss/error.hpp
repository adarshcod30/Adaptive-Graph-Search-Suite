#pragma once

#include <string>
#include <utility>
#include <variant>

namespace agss {

/// A load/parse failure with enough context to point at the offending input.
struct Error {
    std::string message;
    std::string source;  ///< file or origin, may be empty
    int line = 0;        ///< 1-based line number, 0 if not applicable

    std::string what() const {
        std::string s;
        if (!source.empty()) {
            s += source;
            if (line > 0) s += ":" + std::to_string(line);
            s += ": ";
        }
        s += message;
        return s;
    }
};

/// Minimal expected-like result. The original loader signalled failure with a
/// null pointer and let std::stoi throw std::invalid_argument straight through
/// main(), which aborted the process on malformed input. Returning an error
/// value instead makes every failure recoverable and reportable.
template <typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}      // NOLINT(google-explicit-constructor)
    Result(Error error) : data_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & { return std::get<T>(data_); }
    const T& value() const& { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    const Error& error() const& { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

}  // namespace agss
