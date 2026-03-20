#pragma once

#include <SFML/Graphics.hpp>

#include "state_machine.hpp"

class Game
{
public:
    Game();
    void run();

private:
    void processEvents();
    void update(sf::Time deltaTime);
    void render();

    sf::RenderWindow m_window;
    StateContext m_stateContext;
    StateMachine m_stateMachine;
};

void playGame();
