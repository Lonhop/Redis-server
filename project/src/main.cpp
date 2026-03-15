// run using .\build\Debug\redis_server.exe

#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "core/commands.h"
#include "core/serialization.h"
#include "utils/logger.h"

namespace {

// turn the fancy redis protocol objects back into a simple list of words
bool resp_to_args(const redis::core::RespValue& value, std::vector<std::string>& args) {
    using redis::core::RespType;

    // redis users usually send commands as arrays, if its not, then we dont want it
    if (value.type != RespType::Array || value.is_null) {
        return false;
    }

    args.clear();
    args.reserve(value.array_value.size());

    // unpack the data depending on what it is
    for (const auto& item : value.array_value) {
        if (item.is_null) {
            return false;
        }

        switch (item.type) {
            case RespType::BulkString:
            case RespType::SimpleString:
            case RespType::Error:
                args.push_back(item.string_value);
                break;
            case RespType::Integer:
                // numbers become strings because our command parser expects words
                args.push_back(std::to_string(item.integer_value));
                break;
            case RespType::Array:
                // nested arrays in commands are bad
                return false;
        }
    }

    return !args.empty();
}

}

// where the magic (temporarily) happens
int main(int argc, char* argv[]) {
    // setup the logger to yell at us in the terminal, but keep it clean
    redis::utils::LoggerConfig config;
    config.log_to_console = true;
    config.log_to_file = false;
    config.include_location = false;
    redis::utils::Logger::instance().configure(config);

    // our main brain
    // btw, this dies when the program exits, we will fix that when we add networking
    redis::core::KeyValueStore store;

    // run a command straight from the terminal arguments
    // eg: ./server SET mykey myval
    if (argc > 1) {
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc - 1));
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }

        auto command = redis::core::create_command(args);
        std::cout << command->execute(store);
        return 0;
    }

    // pipeline mode: suck up everything from standard input
    std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    if (input.empty()) {
        return 0;
    }

    // chew through the input until we run out
    while (!input.empty()) {
        redis::core::RespParseResult parsed = redis::core::RespParser::parse(input);

        // they didnt send the whole message (fat fingers)
        if (parsed.status == redis::core::RespParseStatus::Incomplete) {
            std::cout << redis::core::RespSerializer::error("ERR incomplete request");
            return 1;
        }

        // they sent absolute garbage (fat fingers)
        if (parsed.status == redis::core::RespParseStatus::Error) {
            std::cout << redis::core::RespSerializer::error("ERR protocol error");
            return 1;
        }

        // translate the network gibberish to normal words
        std::vector<std::string> args;
        if (!resp_to_args(parsed.value, args)) {
            std::cout << redis::core::RespSerializer::error("ERR invalid request");
            return 1;
        }

        // actually do the work and print the result
        auto command = redis::core::create_command(args);
        std::cout << command->execute(store);
        
        // throw away what we just processed and move to the next one
        input.erase(0, parsed.consumed);
    }

    return 0;
}