#pragma once
#include <optional>
struct GameState
{

};

std::optional<GameState> tryGetGameState();

struct InputState
{

};

void sendInputState(const InputState& is);

void initNetwork();
void terminateNetwork();