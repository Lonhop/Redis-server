#include "core/commands.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "config.h"
#include "core/serialization.h"

namespace {

// turn a string to uppercase because users are inconsistent (fat fingers x3)
std::string upper_copy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); }
    );
    return value;
}

// standard redis error response
std::string resp_error(std::string_view message) {
    return redis::core::RespSerializer::error(message);
}

// a dummy command for when things go wrong (i mentioned the fat fingers somewhere already)
class ErrorCommand final : public redis::core::Command {
public:
    explicit ErrorCommand(std::string message) : message_(std::move(message)) {}

    std::string execute(redis::core::KeyValueStore&) override {
        return redis::core::RespSerializer::error(message_);
    }

private:
    std::string message_;
};

}

namespace redis::core {

// setup the data store with our custom hashing and equality functions
KeyValueStore::KeyValueStore()
    : map_(StoreHash(&KeyValueStore::hash_bytes), StoreEq(&KeyValueStore::equal_nodes), 4) {
}

// FNV-1a hash algorithm (dont touch the magic numbers)
std::uint64_t KeyValueStore::hash_bytes(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

// check if two nodes are actually holding the same key
bool KeyValueStore::equal_nodes(const redis::data_structures::HNode* lhs, const redis::data_structures::HNode* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return lhs == rhs;
    }
    return entry_from_node(lhs)->key == entry_from_node(rhs)->key;
}

// caveman magic to get the full entry back from just a node pointer
KeyValueStore::Entry* KeyValueStore::entry_from_node(redis::data_structures::HNode* node) {
    if (node == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Entry*>(reinterpret_cast<char*>(node) - offsetof(Entry, node));
}

// same as above but for when we promise not to change anything
const KeyValueStore::Entry* KeyValueStore::entry_from_node(const redis::data_structures::HNode* node) {
    if (node == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<const Entry*>(reinterpret_cast<const char*>(node) - offsetof(Entry, node));
}

// try to find a node in the map
redis::data_structures::HNode* KeyValueStore::find_node(const std::string& key) {
    Entry probe{};
    probe.key = key;
    probe.node.hcode = static_cast<std::uint32_t>(hash_bytes(key.data(), key.size()));
    return map_.lookup(&probe.node);
}

// try to remove a node from the map entirely
redis::data_structures::HNode* KeyValueStore::pop_node(const std::string& key) {
    Entry probe{};
    probe.key = key;
    probe.node.hcode = static_cast<std::uint32_t>(hash_bytes(key.data(), key.size()));
    return map_.pop(&probe.node);
}

// the actual work of saving data. hidden away so commands dont have to do it
bool KeyValueStore::set(const std::string& key, const std::string& value) {
    // if it exists, just swap the value
    if (auto* node = find_node(key)) {
        entry_from_node(node)->value = value;
        return true;
    }

    // otherwise make a new entry and shove it in there
    auto entry = std::make_unique<Entry>();
    entry->key = key;
    entry->value = value;
    entry->node.hcode = static_cast<std::uint32_t>(hash_bytes(key.data(), key.size()));
    
    map_.insert(&entry->node);
    owned_[key] = std::move(entry); // keep the memory safe here
    return true;
}

// try to fetch data if we have it
bool KeyValueStore::get(const std::string& key, std::string& value) {
    auto* node = find_node(key);
    if (node == nullptr) {
        return false;
    }
    value = entry_from_node(node)->value;
    return true;
}

// remove one or many keys from the store
std::size_t KeyValueStore::del(const std::vector<std::string>& keys) {
    std::size_t removed = 0;
    for (const auto& key : keys) {
        auto* node = pop_node(key);
        if (node != nullptr) {
            owned_.erase(key); // actually delete the memory
            ++removed;
        }
    }
    return removed;
}

// how many things are we holding right now
std::size_t KeyValueStore::size() const noexcept {
    return owned_.size();
}

// setup for the set command
SetCommand::SetCommand(std::string key, std::string value)
    : key_(std::move(key)), value_(std::move(value)) {
}

// execute set logic using the clean store api
std::string SetCommand::execute(KeyValueStore& store) {
    // check if the data is too huge to handle
    if (key_.size() > redis::K_MAX_MSG || value_.size() > redis::K_MAX_MSG) {
        return resp_error("ERR value too large");
    }
    store.set(key_, value_);
    return RespSerializer::simple_string("OK");
}

// setup for the get command
GetCommand::GetCommand(std::string key)
    : key_(std::move(key)) {
}

// execute get logic using the clean store api
std::string GetCommand::execute(KeyValueStore& store) {
    std::string value;
    if (!store.get(key_, value)) {
        return RespSerializer::null_bulk_string(); // redis for nothing here
    }
    return RespSerializer::bulk_string(value);
}

// setup for the del command
DelCommand::DelCommand(std::vector<std::string> keys)
    : keys_(std::move(keys)) {
}

// execute del logic using the clean store api
std::string DelCommand::execute(KeyValueStore& store) {
    return RespSerializer::integer(static_cast<std::int64_t>(store.del(keys_)));
}

// the factory that turns user words into computer actions
CommandPtr create_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return std::make_unique<ErrorCommand>("ERR empty command");
    }

    const std::string name = upper_copy(args[0]);

    // parse the set command
    if (name == "SET") {
        if (args.size() != 3) {
            return std::make_unique<ErrorCommand>("ERR wrong number of arguments for SET");
        }
        return std::make_unique<SetCommand>(args[1], args[2]);
    }

    // parse the get command
    if (name == "GET") {
        if (args.size() != 2) {
            return std::make_unique<ErrorCommand>("ERR wrong number of arguments for GET");
        }
        return std::make_unique<GetCommand>(args[1]);
    }

    // parse the del command
    if (name == "DEL") {
        if (args.size() < 2) {
            return std::make_unique<ErrorCommand>("ERR wrong number of arguments for DEL");
        }
        return std::make_unique<DelCommand>(std::vector<std::string>(args.begin() + 1, args.end()));
    }

    return std::make_unique<ErrorCommand>("ERR unknown command");
}

}