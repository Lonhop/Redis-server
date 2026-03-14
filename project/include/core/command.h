#ifndef REDIS_SERVER_COMMAND_H
#define REDIS_SERVER_COMMAND_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_structures/hash_map.h"

namespace redis::core {

// the actual storage engine. no more shortcuts, we made a real class
class KeyValueStore {
public:
    KeyValueStore();
    ~KeyValueStore() = default;

    // dont try to copy or move this, it will end badly
    KeyValueStore(const KeyValueStore&) = delete;
    KeyValueStore& operator=(const KeyValueStore&) = delete;
    KeyValueStore(KeyValueStore&&) = delete;
    KeyValueStore& operator=(KeyValueStore&&) = delete;

    // standard redis operations
    bool set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    std::size_t del(const std::vector<std::string>& keys);
    std::size_t size() const noexcept;

private:
    // the actual data payload
    struct Entry {
        redis::data_structures::HNode node{};
        std::string key;
        std::string value;
    };

    using StoreEq = std::function<bool(const redis::data_structures::HNode*, const redis::data_structures::HNode*)>;
    using StoreHash = std::function<std::uint64_t(const void*, std::size_t)>;
    using Map = redis::data_structures::HashMap<StoreEq, StoreHash>;

    static std::uint64_t hash_bytes(const void* data, std::size_t size);
    static bool equal_nodes(const redis::data_structures::HNode* lhs, const redis::data_structures::HNode* rhs);
    
    // pointer math black magic
    static Entry* entry_from_node(redis::data_structures::HNode* node);
    static const Entry* entry_from_node(const redis::data_structures::HNode* node);

    redis::data_structures::HNode* find_node(const std::string& key);
    redis::data_structures::HNode* pop_node(const std::string& key);

    Map map_;
    // hold onto the memory so we dont leak it when nodes get moved around
    std::unordered_map<std::string, std::unique_ptr<Entry>> owned_;
};

// the base class for everything the server can actually do
class Command {
public:
    // cleanup for children classes so we dont leak memory like a sieve
    virtual ~Command() = default;

    // run the actual logic and return a redis response string back to the user
    virtual std::string execute(KeyValueStore& store) = 0;
};

// pointer that cleans up itself (is grass green) 
// used so we dont have to manually delete command objects
using CommandPtr = std::unique_ptr<Command>;

}

#endif