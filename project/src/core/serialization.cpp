#include "core/serialization.h"
#include "config.h"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {

// dont let the client trick us into a stack overflow with infinite nesting
constexpr std::size_t k_max_resp_depth = 64;

// redis protocol doesnt like unexpected line breaks in simple strings
bool contains_line_break(std::string_view value) {
    return value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos;
}

// finding where the redis line actually ends
std::size_t find_crlf(std::string_view input, std::size_t start) {
    return input.find("\r\n", start);
}

// fast way to turn text into numbers without using slow streams
bool parse_i64(std::string_view text, std::int64_t& out) {
    if (text.empty()) {
        return false;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

// dont let the payload size explode our memory
bool add_checked(std::size_t value, std::size_t& total) {
    if (value > redis::K_MAX_TOTAL_ARGS_SIZE) {
        return false;
    }
    if (total > redis::K_MAX_TOTAL_ARGS_SIZE - value) {
        return false;
    }
    total += value;
    return true;
}

// return this when we need more data from the network
redis::core::RespParseResult make_incomplete() {
    redis::core::RespParseResult result;
    result.status = redis::core::RespParseStatus::Incomplete;
    return result;
}

// return this when the client sends us garbage (fat fingers)
redis::core::RespParseResult make_error(std::string message) {
    redis::core::RespParseResult result;
    result.status = redis::core::RespParseStatus::Error;
    result.error = std::move(message);
    return result;
}

// return this when everything went perfectly
redis::core::RespParseResult make_ok(redis::core::RespValue value, std::size_t consumed) {
    redis::core::RespParseResult result;
    result.status = redis::core::RespParseStatus::Ok;
    result.value = std::move(value);
    result.consumed = consumed;
    return result;
}

// the main recursive engine for turning raw bytes into objects
redis::core::RespParseResult parse_value(std::string_view input, std::size_t offset, std::size_t depth, std::size_t& total_bytes) {
    using redis::core::RespParseResult;
    using redis::core::RespValue;

    if (depth > k_max_resp_depth) {
        return make_error("RESP nesting too deep"); // bro you are NOT a hacker
    }

    if (offset >= input.size()) {
        return make_incomplete();
    }

    const char prefix = input[offset];

    // handling simple strings, errors, and integers
    if (prefix == '+' || prefix == '-' || prefix == ':') {
        const std::size_t line_end = find_crlf(input, offset + 1);
        if (line_end == std::string_view::npos) {
            return make_incomplete();
        }

        const std::string_view payload = input.substr(offset + 1, line_end - offset - 1);
        const std::size_t consumed = line_end + 2 - offset;

        if (prefix == ':') {
            std::int64_t value = 0;
            if (!parse_i64(payload, value)) {
                return make_error("invalid RESP integer");
            }
            return make_ok(RespValue::integer(value), consumed);
        }

        if (contains_line_break(payload)) {
            return make_error("invalid RESP line");
        }

        if (!add_checked(payload.size(), total_bytes)) {
            return make_error("RESP payload too large");
        }

        if (prefix == '+') {
            return make_ok(RespValue::simple_string(std::string(payload)), consumed);
        }

        return make_ok(RespValue::error(std::string(payload)), consumed);
    }

    // handling bulk strings (the ones with length prefixes)
    if (prefix == '$') {
        const std::size_t line_end = find_crlf(input, offset + 1);
        if (line_end == std::string_view::npos) {
            return make_incomplete();
        }

        const std::string_view length_text = input.substr(offset + 1, line_end - offset - 1);
        std::int64_t bulk_length = 0;
        if (!parse_i64(length_text, bulk_length)) {
            return make_error("invalid bulk string length");
        }

        if (bulk_length < -1) {
            return make_error("invalid bulk string length");
        }

        if (bulk_length == -1) {
            return make_ok(RespValue::null_bulk_string(), line_end + 2 - offset);
        }

        if (bulk_length > static_cast<std::int64_t>(redis::K_MAX_TOTAL_ARGS_SIZE)) {
            return make_error("bulk string too large");
        }

        const std::size_t data_offset = line_end + 2;
        const std::size_t data_length = static_cast<std::size_t>(bulk_length);

        if (data_offset > input.size()) {
            return make_incomplete();
        }

        if (input.size() - data_offset < data_length + 2) {
            return make_incomplete(); // wait for the actual data to arrive
        }

        if (input[data_offset + data_length] != '\r' || input[data_offset + data_length + 1] != '\n') {
            return make_error("invalid bulk string terminator");
        }

        if (!add_checked(data_length, total_bytes)) {
            return make_error("RESP payload too large");
        }

        const std::string_view payload = input.substr(data_offset, data_length);
        return make_ok(RespValue::bulk_string(std::string(payload)), data_offset + data_length + 2 - offset);
    }

    // handling arrays (recursive stuff)
    if (prefix == '*') {
        const std::size_t line_end = find_crlf(input, offset + 1);
        if (line_end == std::string_view::npos) {
            return make_incomplete();
        }

        const std::string_view count_text = input.substr(offset + 1, line_end - offset - 1);
        std::int64_t count = 0;
        if (!parse_i64(count_text, count)) {
            return make_error("invalid array length");
        }

        if (count < -1) {
            return make_error("invalid array length");
        }

        if (count == -1) {
            return make_ok(RespValue::null_array(), line_end + 2 - offset);
        }

        if (count > static_cast<std::int64_t>(redis::K_MAX_ARGS)) {
            return make_error("array too large");
        }

        std::vector<RespValue> values;
        values.reserve(static_cast<std::size_t>(count));

        std::size_t consumed = line_end + 2 - offset;
        for (std::int64_t i = 0; i < count; ++i) {
            RespParseResult element = parse_value(input, offset + consumed, depth + 1, total_bytes);
            if (element.status != redis::core::RespParseStatus::Ok) {
                return element;
            }
            consumed += element.consumed;
            values.push_back(std::move(element.value));
        }

        return make_ok(RespValue::array(std::move(values)), consumed);
    }

    return make_error("unknown RESP type");
}

}

namespace redis::core {

// builders for the value objects
RespValue RespValue::simple_string(std::string value) {
    RespValue result;
    result.type = RespType::SimpleString;
    result.string_value = std::move(value);
    return result;
}

RespValue RespValue::error(std::string value) {
    RespValue result;
    result.type = RespType::Error;
    result.string_value = std::move(value);
    return result;
}

RespValue RespValue::integer(std::int64_t value) {
    RespValue result;
    result.type = RespType::Integer;
    result.integer_value = value;
    return result;
}

RespValue RespValue::bulk_string(std::string value) {
    RespValue result;
    result.type = RespType::BulkString;
    result.string_value = std::move(value);
    return result;
}

RespValue RespValue::null_bulk_string() {
    RespValue result;
    result.type = RespType::BulkString;
    result.is_null = true;
    return result;
}

RespValue RespValue::array(std::vector<RespValue> values) {
    RespValue result;
    result.type = RespType::Array;
    result.array_value = std::move(values);
    return result;
}

RespValue RespValue::null_array() {
    RespValue result;
    result.type = RespType::Array;
    result.is_null = true;
    return result;
}

// public entry points for parsing
RespParseResult RespParser::parse(std::string_view buffer) {
    std::size_t total_bytes = 0;
    return parse_value(buffer, 0, 0, total_bytes);
}

RespParseResult RespParser::parse(const std::string& buffer) {
    return parse(std::string_view(buffer.data(), buffer.size()));
}

RespParseResult RespParser::parse(const std::vector<char>& buffer) {
    if (buffer.empty()) {
        return parse(std::string_view{});
    }
    return parse(std::string_view(buffer.data(), buffer.size()));
}

// taking objects and making them raw strings again
std::string RespSerializer::serialize(const RespValue& value) {
    switch (value.type) {
        case RespType::SimpleString:
            return simple_string(value.string_value);
        case RespType::Error:
            return error(value.string_value);
        case RespType::Integer:
            return integer(value.integer_value);
        case RespType::BulkString:
            if (value.is_null) {
                return null_bulk_string();
            }
            return bulk_string(value.string_value);
        case RespType::Array:
            if (value.is_null) {
                return null_array();
            }
            return array(value.array_value);
        default:
            throw std::invalid_argument("unknown RESP value type");
    }
}

std::string RespSerializer::simple_string(std::string_view value) {
    if (contains_line_break(value)) {
        throw std::invalid_argument("simple string cannot contain CR or LF");
    }

    std::string out;
    out.reserve(1 + value.size() + 2);
    out.push_back('+');
    out.append(value.data(), value.size());
    out.append("\r\n");
    return out;
}

std::string RespSerializer::error(std::string_view value) {
    if (contains_line_break(value)) {
        throw std::invalid_argument("error string cannot contain CR or LF");
    }

    std::string out;
    out.reserve(1 + value.size() + 2);
    out.push_back('-');
    out.append(value.data(), value.size());
    out.append("\r\n");
    return out;
}

std::string RespSerializer::integer(std::int64_t value) {
    std::string out;
    out.push_back(':');
    out += std::to_string(value);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::bulk_string(std::string_view value) {
    std::string out;
    out.reserve(1 + 20 + 2 + value.size() + 2);
    out.push_back('$');
    out += std::to_string(value.size());
    out.append("\r\n");
    out.append(value.data(), value.size());
    out.append("\r\n");
    return out;
}

std::string RespSerializer::null_bulk_string() {
    return "$-1\r\n";
}

std::string RespSerializer::array(const std::vector<RespValue>& values) {
    if (values.size() > redis::K_MAX_ARGS) {
        throw std::invalid_argument("array too large");
    }

    std::string out;
    out.push_back('*');
    out += std::to_string(values.size());
    out.append("\r\n");

    for (const RespValue& value : values) {
        out += serialize(value);
    }

    return out;
}

std::string RespSerializer::null_array() {
    return "*-1\r\n";
}

// turn a list of words into a full redis command string
std::string RespSerializer::command(const std::vector<std::string>& args) {
    if (args.size() > redis::K_MAX_ARGS) {
        throw std::invalid_argument("too many command arguments");
    }

    std::size_t total_size = 0;
    for (const std::string& arg : args) {
        if (!add_checked(arg.size(), total_size)) {
            throw std::invalid_argument("command payload too large");
        }
    }

    std::string out;
    out.push_back('*');
    out += std::to_string(args.size());
    out.append("\r\n");

    for (const std::string& arg : args) {
        out += bulk_string(arg);
    }

    return out;
}

}