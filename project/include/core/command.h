#ifndef REDIS_SERVER_COMMAND_H
#define REDIS_SERVER_COMMAND_H

#include <memory>
#include <string>

#include "data_structures/hash_map.h"

namespace redis::core {

using KeyValueStore = redis::data_structures::StringHashMap;

class Command {
public:
    virtual ~Command() = default;
    virtual std::string execute(KeyValueStore& store) = 0;
};

using CommandPtr = std::unique_ptr<Command>;

}

#endif