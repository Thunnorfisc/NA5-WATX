#pragma once

#include <SFML/Graphics.hpp>

#include <optional>

enum class StateId
{
    Login,
    MainMenu,
    CoreGame
};

struct StateContext
{
    sf::RenderWindow& window;
    std::optional<sf::Event> event;
};

class StateMachine;

class State
{
public:
    State(StateMachine& stateMachine, StateContext& context);
    virtual ~State() = default;

    virtual void handleEvent(const sf::Event& event);
    virtual void update(sf::Time deltaTime) = 0;
    virtual void render() = 0;

protected:
    void requestStateChange(StateId stateId);
    [[nodiscard]] StateContext& context();
    [[nodiscard]] const StateContext& context() const;

private:
    StateMachine& m_stateMachine;
    StateContext& m_context;
};
