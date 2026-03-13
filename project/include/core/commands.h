#ifndef REDIS_SERVER_COMMANDS_H
#define REDIS_SERVER_COMMANDS_H

#include <string>
#include <vector>

#include "core/command.h"

namespace redis::core {

// below are the commands themselves

// save data to the store so we can find it later
class SetCommand final : public Command {
public:
    SetCommand(std::string key, std::string value);
    std::string execute(KeyValueStore& store) override;

private:
    std::string key_;
    std::string value_;
};

// read data back from the store (if it even exists)
class GetCommand final : public Command {
public:
    explicit GetCommand(std::string key);
    std::string execute(KeyValueStore& store) override;

private:
    std::string key_;
};

// remove data from the store because we dont want it anymore
class DelCommand final : public Command {
public:
    explicit DelCommand(std::vector<std::string> keys);
    std::string execute(KeyValueStore& store) override;

private:
    std::vector<std::string> keys_;
};

// setup the data storage where all the magic happens
KeyValueStore make_command_store();

// wipe everything in that storage and start over with a clean slate
void clear_command_store(KeyValueStore& store);

// turn raw strings into a command object
// basically caveman magic to make the server do work
CommandPtr create_command(const std::vector<std::string>& args);

}

#endif