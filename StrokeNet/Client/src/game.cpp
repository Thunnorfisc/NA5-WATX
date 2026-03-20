#include "game.hpp"
#include "network.hpp"

#include <optional>

namespace
{
    class NetworkSession
    {
    public:
        NetworkSession()
        {           
            initNetwork();
        }

        ~NetworkSession()
        {
            terminateNetwork();
        }
    };
}

Game::Game() :
    m_window(sf::VideoMode({ 1600, 900 }), "Testing window"),
    m_stateContext{ m_window },
    m_stateMachine(m_stateContext)
{
    m_window.setFramerateLimit(60);
    m_stateMachine.changeState(StateId::MainMenu);
}

void Game::run()
{
    NetworkSession networkSession;
    sf::Clock deltaClock;

    while (m_window.isOpen())
    {
        const sf::Time deltaTime = deltaClock.restart();

        processEvents();
        update(deltaTime);
        render();
    }
}

void Game::processEvents()
{
    m_stateContext.event.reset();

    while (const std::optional event = m_window.pollEvent())
    {
        m_stateContext.event = *event;

        if (event->is<sf::Event::Closed>())
        {
            m_window.close();
            continue;
        }

        m_stateMachine.handleEvent(*event);
    }

    m_stateContext.event.reset();
}

void Game::update(sf::Time deltaTime)
{
    m_stateMachine.update(deltaTime);
}

void Game::render()
{
    m_window.clear();
    m_stateMachine.render();
    m_window.display();
}

void playGame()
{
    Game game;
    game.run();
}
