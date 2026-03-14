#ifndef REDIS_SERVER_SERIALIZATION_H
#define REDIS_SERVER_SERIALIZATION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace redis::core {

// all the different types of data redis understands
enum class RespType : std::uint8_t {
    SimpleString = 0,
    Error = 1,
    Integer = 2,
    BulkString = 3,
    Array = 4
};

// tells us if we actually finished reading the data or if we need to wait for more
enum class RespParseStatus : std::uint8_t {
    Ok = 0,
    Incomplete = 1,
    Error = 2
};

// a big container that can hold any kind of redis value
// basically a poor mans union but safer
struct RespValue {
    RespType type = RespType::BulkString;
    bool is_null = false; // for when redis sends back nothing (happens a lot)
    std::string string_value;
    std::int64_t integer_value = 0;
    std::vector<RespValue> array_value;

    // helper functions to build responses, after all we want to make life easier
    static RespValue simple_string(std::string value);
    static RespValue error(std::string value);
    static RespValue integer(std::int64_t value);
    static RespValue bulk_string(std::string value);
    static RespValue null_bulk_string();
    static RespValue array(std::vector<RespValue> values);
    static RespValue null_array();
};

// what we get back after trying to make sense of the raw bytes
struct RespParseResult {
    RespParseStatus status = RespParseStatus::Incomplete;
    RespValue value{};
    std::size_t consumed = 0; // how many bytes we actually managed to read
    std::string error; // why it failed (usually because of a typo (fat fingers lmao))
};

// the logic that turns raw network bytes into useful objects
class RespParser {
public:
    // try to find a complete redis message in the buffer
    static RespParseResult parse(std::string_view buffer);
    static RespParseResult parse(const std::string& buffer);
    static RespParseResult parse(const std::vector<char>& buffer);
};

// the logic that turns objects back into raw bytes to send over the wire
class RespSerializer {
public:
    // turn a value object into a string ready for the client
    static std::string serialize(const RespValue& value);

    // direct serialization helpers so you dont have to build an object first
    static std::string simple_string(std::string_view value);
    static std::string error(std::string_view value);
    static std::string integer(std::int64_t value);
    static std::string bulk_string(std::string_view value);
    static std::string null_bulk_string();
    static std::string array(const std::vector<RespValue>& values);
    static std::string null_array();
    
    // special helper to turn a list of strings into a redis command
    static std::string command(const std::vector<std::string>& args);
};

}

#endif