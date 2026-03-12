#ifndef REDIS_SERVER_SERIALIZATION_H
#define REDIS_SERVER_SERIALIZATION_H

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.h"

namespace redis::core::serialization {

class Value {
public:
    enum class Type : std::uint8_t {
        Nil = static_cast<std::uint8_t>(redis::SerialType::NIL),
        Error = static_cast<std::uint8_t>(redis::SerialType::ERR),
        String = static_cast<std::uint8_t>(redis::SerialType::STR),
        Integer = static_cast<std::uint8_t>(redis::SerialType::INT),
        Array = static_cast<std::uint8_t>(redis::SerialType::ARR),
    };

    struct ErrorData {
        redis::ErrorCode code = redis::ErrorCode::OK;
        std::string message;

        [[nodiscard]] bool operator==(const ErrorData& other) const noexcept {
            return code == other.code && message == other.message;
        }
    };

    Value() noexcept = default;

    [[nodiscard]] static Value nil() noexcept {
        return Value{};
    }

    [[nodiscard]] static Value error(redis::ErrorCode code, std::string message) {
        Value v;
        v.type_ = Type::Error;
        v.error_ = ErrorData{code, std::move(message)};
        return v;
    }

    [[nodiscard]] static Value string(std::string value) {
        Value v;
        v.type_ = Type::String;
        v.string_ = std::move(value);
        return v;
    }

    [[nodiscard]] static Value integer(std::int64_t value) noexcept {
        Value v;
        v.type_ = Type::Integer;
        v.integer_ = value;
        return v;
    }

    [[nodiscard]] static Value array(std::vector<Value> values) {
        Value v;
        v.type_ = Type::Array;
        v.array_ = std::move(values);
        return v;
    }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_nil() const noexcept { return type_ == Type::Nil; }
    [[nodiscard]] bool is_error() const noexcept { return type_ == Type::Error; }
    [[nodiscard]] bool is_string() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool is_integer() const noexcept { return type_ == Type::Integer; }
    [[nodiscard]] bool is_array() const noexcept { return type_ == Type::Array; }

    [[nodiscard]] const ErrorData& as_error() const { return error_; }
    [[nodiscard]] const std::string& as_string() const { return string_; }
    [[nodiscard]] std::int64_t as_integer() const noexcept { return integer_; }
    [[nodiscard]] const std::vector<Value>& as_array() const noexcept { return array_; }
    [[nodiscard]] std::vector<Value>& as_array() noexcept { return array_; }

    [[nodiscard]] bool operator==(const Value& other) const noexcept {
        return type_ == other.type_ &&
               error_ == other.error_ &&
               string_ == other.string_ &&
               integer_ == other.integer_ &&
               array_ == other.array_;
    }

private:
    Type type_ = Type::Nil;
    ErrorData error_{};
    std::string string_{};
    std::int64_t integer_ = 0;
    std::vector<Value> array_{};
};

struct SerializeOptions {
    std::size_t max_bytes = redis::K_MAX_MSG;
    std::size_t max_depth = 64;
    std::size_t max_array_items = redis::K_MAX_ARGS;
};

struct DeserializeOptions {
    std::size_t max_bytes = redis::K_MAX_MSG;
    std::size_t max_depth = 64;
    std::size_t max_array_items = redis::K_MAX_ARGS;
    bool require_full_consumption = true;
};

struct DeserializeResult {
    bool ok = false;
    Value value{};
    std::size_t consumed = 0;
    std::string error;
};

namespace detail {

[[nodiscard]] inline bool checked_add(std::size_t lhs, std::size_t rhs, std::size_t& out) noexcept {
    if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

[[nodiscard]] inline bool ensure_room(std::size_t current, std::size_t extra, std::size_t max_bytes) noexcept {
    std::size_t total = 0;
    return checked_add(current, extra, total) && total <= max_bytes;
}

inline void append_u32_le(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
}

inline void append_i32_le(std::vector<std::byte>& out, std::int32_t value) {
    append_u32_le(out, static_cast<std::uint32_t>(value));
}

inline void append_i64_le(std::vector<std::byte>& out, std::int64_t value) {
    const auto u = static_cast<std::uint64_t>(value);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((u >> shift) & 0xFFu));
    }
}

[[nodiscard]] inline bool read_u32_le(std::span<const std::byte> in, std::size_t offset, std::uint32_t& value) noexcept {
    if (offset > in.size() || in.size() - offset < sizeof(std::uint32_t)) {
        return false;
    }
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset + i])) << (8u * i);
    }
    return true;
}

[[nodiscard]] inline bool read_i32_le(std::span<const std::byte> in, std::size_t offset, std::int32_t& value) noexcept {
    std::uint32_t temp = 0;
    if (!read_u32_le(in, offset, temp)) {
        return false;
    }
    value = static_cast<std::int32_t>(temp);
    return true;
}

[[nodiscard]] inline bool read_i64_le(std::span<const std::byte> in, std::size_t offset, std::int64_t& value) noexcept {
    if (offset > in.size() || in.size() - offset < sizeof(std::int64_t)) {
        return false;
    }
    std::uint64_t temp = 0;
    for (unsigned i = 0; i < 8; ++i) {
        temp |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + i])) << (8u * i);
    }
    value = static_cast<std::int64_t>(temp);
    return true;
}

inline bool serialize_impl(const Value& value, std::vector<std::byte>& out, const SerializeOptions& options, std::size_t depth) {
    if (depth > options.max_depth) {
        return false;
    }

    if (!ensure_room(out.size(), 1, options.max_bytes)) {
        return false;
    }
    out.push_back(static_cast<std::byte>(value.type()));

    switch (value.type()) {
    case Value::Type::Nil:
        return true;

    case Value::Type::Error: {
        const auto& err = value.as_error();
        if (err.message.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        if (!ensure_room(out.size(), sizeof(std::int32_t) + sizeof(std::uint32_t) + err.message.size(), options.max_bytes)) {
            return false;
        }
        append_i32_le(out, static_cast<std::int32_t>(err.code));
        append_u32_le(out, static_cast<std::uint32_t>(err.message.size()));
        const auto* ptr = reinterpret_cast<const std::byte*>(err.message.data());
        out.insert(out.end(), ptr, ptr + err.message.size());
        return true;
    }

    case Value::Type::String: {
        const auto& s = value.as_string();
        if (s.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        if (!ensure_room(out.size(), sizeof(std::uint32_t) + s.size(), options.max_bytes)) {
            return false;
        }
        append_u32_le(out, static_cast<std::uint32_t>(s.size()));
        const auto* ptr = reinterpret_cast<const std::byte*>(s.data());
        out.insert(out.end(), ptr, ptr + s.size());
        return true;
    }

    case Value::Type::Integer:
        if (!ensure_room(out.size(), sizeof(std::int64_t), options.max_bytes)) {
            return false;
        }
        append_i64_le(out, value.as_integer());
        return true;

    case Value::Type::Array: {
        const auto& arr = value.as_array();
        if (arr.size() > options.max_array_items || arr.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        if (!ensure_room(out.size(), sizeof(std::uint32_t), options.max_bytes)) {
            return false;
        }
        append_u32_le(out, static_cast<std::uint32_t>(arr.size()));
        for (const auto& item : arr) {
            if (!serialize_impl(item, out, options, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    }

    return false;
}

inline DeserializeResult deserialize_impl(std::span<const std::byte> input, const DeserializeOptions& options, std::size_t depth) {
    DeserializeResult result;
    if (depth > options.max_depth) {
        result.error = "maximum nesting depth exceeded";
        return result;
    }
    if (input.empty()) {
        result.error = "buffer is empty";
        return result;
    }
    if (input.size() > options.max_bytes) {
        result.error = "buffer exceeds configured size limit";
        return result;
    }

    const auto raw_type = std::to_integer<std::uint8_t>(input.front());
    if (!redis::is_valid_serial_type(raw_type)) {
        result.error = "invalid serialized type";
        return result;
    }

    const auto type = static_cast<Value::Type>(raw_type);
    switch (type) {
    case Value::Type::Nil:
        result.ok = true;
        result.value = Value::nil();
        result.consumed = 1;
        return result;

    case Value::Type::Error: {
        std::int32_t code = 0;
        std::uint32_t len = 0;
        if (!read_i32_le(input, 1, code) || !read_u32_le(input, 1 + sizeof(std::int32_t), len)) {
            result.error = "truncated error header";
            return result;
        }
        const std::size_t payload_offset = 1 + sizeof(std::int32_t) + sizeof(std::uint32_t);
        if (payload_offset > input.size() || input.size() - payload_offset < len) {
            result.error = "truncated error payload";
            return result;
        }
        result.ok = true;
        result.value = Value::error(static_cast<redis::ErrorCode>(code), std::string(reinterpret_cast<const char*>(input.data() + payload_offset), len));
        result.consumed = payload_offset + len;
        return result;
    }

    case Value::Type::String: {
        std::uint32_t len = 0;
        if (!read_u32_le(input, 1, len)) {
            result.error = "truncated string header";
            return result;
        }
        const std::size_t payload_offset = 1 + sizeof(std::uint32_t);
        if (payload_offset > input.size() || input.size() - payload_offset < len) {
            result.error = "truncated string payload";
            return result;
        }
        result.ok = true;
        result.value = Value::string(std::string(reinterpret_cast<const char*>(input.data() + payload_offset), len));
        result.consumed = payload_offset + len;
        return result;
    }

    case Value::Type::Integer: {
        std::int64_t value = 0;
        if (!read_i64_le(input, 1, value)) {
            result.error = "truncated integer payload";
            return result;
        }
        result.ok = true;
        result.value = Value::integer(value);
        result.consumed = 1 + sizeof(std::int64_t);
        return result;
    }

    case Value::Type::Array: {
        std::uint32_t len = 0;
        if (!read_u32_le(input, 1, len)) {
            result.error = "truncated array header";
            return result;
        }
        if (len > options.max_array_items) {
            result.error = "array item count exceeds configured limit";
            return result;
        }

        std::size_t consumed = 1 + sizeof(std::uint32_t);
        
        std::size_t remaining_bytes = input.size() - consumed;
        std::size_t safe_reserve = std::min<std::size_t>(len, remaining_bytes);
        
        std::vector<Value> items;
        items.reserve(safe_reserve);

        for (std::uint32_t i = 0; i < len; ++i) {
            if (consumed > input.size()) {
                result.error = "array payload is truncated";
                return result;
            }
            auto child = deserialize_impl(input.subspan(consumed), options, depth + 1);
            if (!child.ok) {
                result.error = child.error;
                return result;
            }
            if (child.consumed == 0) {
                result.error = "deserializer made no progress";
                return result;
            }
            std::size_t new_consumed = 0;
            if (!checked_add(consumed, child.consumed, new_consumed) || new_consumed > input.size()) {
                result.error = "array payload exceeds buffer";
                return result;
            }
            consumed = new_consumed;
            items.push_back(std::move(child.value));
        }

        result.ok = true;
        result.value = Value::array(std::move(items));
        result.consumed = consumed;
        return result;
    }
    }

    result.error = "unsupported serialized type";
    return result;
}

}

[[nodiscard]] inline std::optional<std::vector<std::byte>> serialize(const Value& value, const SerializeOptions& options = {}) {
    if (options.max_bytes == 0 || options.max_depth == 0) {
        return std::nullopt;
    }

    std::vector<std::byte> out;
    out.reserve(64);
    if (!detail::serialize_impl(value, out, options, 0)) {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] inline DeserializeResult deserialize(std::span<const std::byte> input, const DeserializeOptions& options = {}) {
    auto result = detail::deserialize_impl(input, options, 0);
    if (!result.ok) {
        return result;
    }
    if (options.require_full_consumption && result.consumed != input.size()) {
        result.ok = false;
        result.error = "trailing bytes after serialized value";
    }
    return result;
}

[[nodiscard]] inline std::optional<std::string> serialize_to_string(const Value& value, const SerializeOptions& options = {}) {
    const auto bytes = serialize(value, options);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

[[nodiscard]] inline DeserializeResult deserialize_from_string(std::string_view input, const DeserializeOptions& options = {}) {
    return deserialize(std::span<const std::byte>(reinterpret_cast<const std::byte*>(input.data()), input.size()), options);
}

}

#endif
