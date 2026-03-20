#include "network.hpp"

#include <mutex>
namespace
{
	std::mutex s_gameStateMutex;
	GameState s_gameState;
	InputState s_inputState;
}
std::optional<GameState> tryGetGameState()
{
	std::unique_lock lock(s_gameStateMutex, std::try_to_lock);
	if (!lock.owns_lock())
	{
		return {};
	}

	return s_gameState;
}
void sendInputState(const InputState& is)
{
	s_inputState = is;
	// more logic here...
}

void initNetwork()
{

}
void terminateNetwork()
{

}
