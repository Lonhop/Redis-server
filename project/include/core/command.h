#ifndef REDIS_SERVER_COMMAND_H
#define REDIS_SERVER_COMMAND_H

#include <memory>
#include <string>

#include "data_structures/hash_map.h"

namespace redis::core {

// a shortcut for storage so we don't have to type the whole path every time
using KeyValueStore = redis::data_structures::StringHashMap;

// the base class for everything the server can actually do
class Command {
public:
    // cleanup for children classes so we don't leak memory like a sieve
    virtual ~Command() = default;

    // run the actual logic and return a redis response string back to the user
    virtual std::string execute(KeyValueStore& store) = 0;
};

// pointer that cleans up itself (is grass green) 
// used so we don't have to manually delete command objects
using CommandPtr = std::unique_ptr<Command>;

}

#endif