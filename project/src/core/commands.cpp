#include "core/commands.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

#include "config.h"
#include "core/serialization.h"

namespace {

using redis::core::KeyValueStore;
using redis::data_structures::HNode;

// the actual data we store in the hash map
struct Entry {
    HNode node{};    // the node part that the hash map cares about
    std::string key; // the actual key
    std::string value; // the actual value
};

// caveman magic to get the full entry back from just a node pointer
Entry* entry_from_node(HNode* node) {
    if (node == nullptr) {
        return nullptr;
    }
    // we subtract the offset of the node member to find the start of the struct
    return reinterpret_cast<Entry*>(
        reinterpret_cast<char*>(node) - offsetof(Entry, node)
    );
}

// same as above but for when we promise not to change anything
const Entry* entry_from_node(const HNode* node) {
    if (node == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<const Entry*>(
        reinterpret_cast<const char*>(node) - offsetof(Entry, node)
    );
}

// helper to turn a key into a number for the map
std::uint64_t hash_key(std::string_view key) {
    return KeyValueStore::default_hash(key.data(), key.size());
}

// check if two nodes are actually holding the same key
bool same_node_key(const HNode* lhs, const HNode* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return lhs == rhs;
    }
    return entry_from_node(lhs)->key == entry_from_node(rhs)->key;
}

// check if a node matches a specific string key
bool same_string_key(const HNode* node, const std::string& key) {
    return node != nullptr && entry_from_node(node)->key == key;
}

// try to find a node in the store without making a mess
HNode* find_node(KeyValueStore& store, const std::string& key) {
    return store.find(key, hash_key(key), &same_string_key);
}

// standard redis ok response
std::string resp_ok() {
    return redis::core::RespSerializer::simple_string("OK");
}

// standard redis error response
std::string resp_error(std::string_view message) {
    return redis::core::RespSerializer::error(message);
}

// a dummy command for when things go wrong (i mentioned the fat fingers somewhere already)
class ErrorCommand final : public redis::core::Command {
public:
    explicit ErrorCommand(std::string message) : message_(std::move(message)) {}
    std::string execute(KeyValueStore&) override {
        return resp_error(message_);
    }

private:
    std::string message_;
};

// turn a string to uppercase because users are inconsistent (fat fingers x3)
std::string upper_copy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); }
    );
    return value;
}

}

namespace redis::core {

// setup for the set command
SetCommand::SetCommand(std::string key, std::string value)
    : key_(std::move(key)), value_(std::move(value)) {}

// the actual work of saving data
std::string SetCommand::execute(KeyValueStore& store) {
    try {
        // check if the data is too huge to handle
        if (key_.size() > redis::K_MAX_MSG || value_.size() > redis::K_MAX_MSG) {
            return resp_error("value too large");
        }

        // if it exists, just swap the value
        if (HNode* found = find_node(store, key_)) {
            entry_from_node(found)->value = value_;
            return resp_ok();
        }

        // otherwise make a new entry and shove it in there
        Entry* entry = new Entry{};
        entry->key = key_;
        entry->value = value_;
        entry->node.hcode = hash_key(entry->key);

        if (!store.insert(&entry->node)) {
            delete entry; // clean up our mess if it failed
            return resp_error("insert failed");
        }

        return resp_ok();
    } catch (const std::bad_alloc&) {
        return resp_error("out of memory"); // well this is awkward (insert no memory meme here with megamind)
    } catch (...) {
        return resp_error("internal error"); // something went really wrong
    }
}

// setup for the get command
GetCommand::GetCommand(std::string key)
    : key_(std::move(key)) {}

// try to fetch data if we have it
std::string GetCommand::execute(KeyValueStore& store) {
    try {
        HNode* found = find_node(store, key_);
        if (found == nullptr) {
            return RespSerializer::null_bulk_string(); // redis for nothing here
        }

        return RespSerializer::bulk_string(entry_from_node(found)->value);
    } catch (...) {
        return resp_error("internal error");
    }
}

// setup for the del command
DelCommand::DelCommand(std::vector<std::string> keys)
    : keys_(std::move(keys)) {}

// remove one or many keys from the store
std::string DelCommand::execute(KeyValueStore& store) {
    try {
        std::int64_t removed = 0;

        for (const std::string& key : keys_) {
            HNode* erased = store.erase(key, hash_key(key), &same_string_key);
            if (erased != nullptr) {
                delete entry_from_node(erased); // don't forget to actually delete the data
                ++removed;
            }
        }

        return RespSerializer::integer(removed);
    } catch (...) {
        return resp_error("internal error");
    }
}

// create a fresh new store with our custom logic
KeyValueStore make_command_store() {
    return KeyValueStore(
        KeyValueStore::default_hash,
        &same_node_key
    );
}

// destroy everything in the store safely
void clear_command_store(KeyValueStore& store) {
    std::vector<HNode*> nodes;
    nodes.reserve(store.size());

    // grab all the nodes first
    store.for_each([&nodes](HNode* node) {
        if (node != nullptr) {
            nodes.push_back(node);
        }
    });

    store.clear();

    // delete them properly one by one
    for (HNode* node : nodes) {
        delete entry_from_node(node);
    }
}

// the factory that turns user words into computer actions
CommandPtr create_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return std::make_unique<ErrorCommand>("empty command");
    }

    const std::string name = upper_copy(args[0]);

    // parse the set command
    if (name == "SET") {
        if (args.size() != 3) {
            return std::make_unique<ErrorCommand>("wrong number of arguments for SET");
        }
        return std::make_unique<SetCommand>(args[1], args[2]);
    }

    // parse the get command
    if (name == "GET") {
        if (args.size() != 2) {
            return std::make_unique<ErrorCommand>("wrong number of arguments for GET");
        }
        return std::make_unique<GetCommand>(args[1]);
    }

    // parse the del command
    if (name == "DEL") {
        if (args.size() < 2) {
            return std::make_unique<ErrorCommand>("wrong number of arguments for DEL");
        }
        return std::make_unique<DelCommand>(
            std::vector<std::string>(args.begin() + 1, args.end())
        );
    }

    return std::make_unique<ErrorCommand>("unknown command");
}

}