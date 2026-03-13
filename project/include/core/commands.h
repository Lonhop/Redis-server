#ifndef REDIS_SERVER_COMMANDS_H
#define REDIS_SERVER_COMMANDS_H

#include <string>
#include <vector>

#include "core/command.h"

namespace redis::core {

class SetCommand final : public Command {
public:
    SetCommand(std::string key, std::string value);
    std::string execute(KeyValueStore& store) override;

private:
    std::string key_;
    std::string value_;
};

class GetCommand final : public Command {
public:
    explicit GetCommand(std::string key);
    std::string execute(KeyValueStore& store) override;

private:
    std::string key_;
};

class DelCommand final : public Command {
public:
    explicit DelCommand(std::vector<std::string> keys);
    std::string execute(KeyValueStore& store) override;

private:
    std::vector<std::string> keys_;
};

KeyValueStore make_command_store();
void clear_command_store(KeyValueStore& store);
CommandPtr create_command(const std::vector<std::string>& args);

}

#endif