#pragma once
#include "shared_protocol.hpp"
#include <optional>

std::optional<GameState> tryGetGameState();

void sendInputState(const InputState& is);

void initNetwork();
void terminateNetwork();
