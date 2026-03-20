//#include <optional>
//#include <SFML/Audio/Music.hpp>
//#include <SFML/Audio/SoundStream.hpp>
//#include <SFML/Graphics.hpp>
//#include "Drawing.hpp"
//void playGame()
//{
//    // Create the main window
//    sf::RenderWindow window(sf::VideoMode({ 800, 600 }), "Testing window");
//
//    // Load a sprite to display
//    const sf::Texture texture("resources/jjbruno.png");
//    sf::Sprite sprite(texture);
//
//    // Create a graphical text to display
//    const sf::Font font("resources/Cinzel-Regular.ttf");
//    sf::Text text(font, "wakata", 50);
//
//    // Load a music to play
//    sf::Music music("resources/jaedenbgm.wav");
//
//    // Play the music
//    music.play();
//
//    // FOR TESTING FIRST
//    Drawing drawing;
//    Stroke currentStroke;
//    RenderStroke currentRender;
//    bool isDrawing = false;
//
//    // HARDCODED FOR NOW BRUH
//    currentStroke.colour = sf::Color::White;
//    currentStroke.thickness = 6.f;
//    currentStroke.id = 0;
//    uint32_t nextId = 1;
//
//    // Start the game loop
//    while (window.isOpen())
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
