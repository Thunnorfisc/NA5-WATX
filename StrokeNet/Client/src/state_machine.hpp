#pragma once

#include "state.hpp"

#include <memory>
#include <optional>

class StateMachine
{
public:
    explicit StateMachine(StateContext& context);

    void changeState(StateId stateId);
    void handleEvent(const sf::Event& event);
    void update(sf::Time deltaTime);
    void render();

private:
    void applyPendingStateChange();
    [[nodiscard]] std::unique_ptr<State> createState(StateId stateId);

    StateContext& m_context;
    std::unique_ptr<State> m_currentState;
    std::optional<StateId> m_pendingState;
};
