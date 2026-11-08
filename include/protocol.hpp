#pragma once

#include "matching_engine.hpp"

#include <string>
#include <vector>

enum class CommandType {
    Add,
    Cancel,
    Get,
    Trades,
    Subscribe,
    Unsubscribe,
    Invalid
};

struct ParsedCommand {
    CommandType type = CommandType::Invalid;
    Order order;
};

struct CommandResult {
    std::string reply;
    bool mutates = false;
    std::vector <Trade> new_trades;
};

bool ParseInt(const std::string &s, int &value);

ParsedCommand ParseCommand(const std::string &msg);

CommandResult HandleCommand(
    MatchingEngine &engine,
    const Order &order,
    const CommandType &type
);
