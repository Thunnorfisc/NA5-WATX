#include <optional>
#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/SoundStream.hpp>
#include <SFML/Graphics.hpp>
#include "Drawing.hpp"
void playGame()
{
    // Create the main window
    sf::RenderWindow window(sf::VideoMode({ 800, 600 }), "Testing window");

    // Load a sprite to display
    const sf::Texture texture("resources/jjbruno.png");
    sf::Sprite sprite(texture);

    // Create a graphical text to display
    const sf::Font font("resources/Cinzel-Regular.ttf");
    sf::Text text(font, "wakata", 50);

    // Load a music to play
    sf::Music music("resources/jaedenbgm.wav");

    // Play the music
    music.play();

    // FOR TESTING FIRST
    Drawing drawing;
    Stroke currentStroke;
    RenderStroke currentRender;
    bool isDrawing = false;

    // HARDCODED FOR NOW BRUH
    currentStroke.colour = sf::Color::White;
    currentStroke.thickness = 6.f;
    currentStroke.id = 0;
    uint32_t nextId = 1;

    // Start the game loop
    while (window.isOpen())
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
            // Close window: exit
            if (event->is<sf::Event::Closed>())
                window.close();

            if (const auto* pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (pressed->button == sf::Mouse::Button::Left) {
                    isDrawing = true;
                    currentStroke = {};
                    currentStroke.id = nextId++;
                    currentStroke.colour = sf::Color::White;
                    currentStroke.thickness = 6.f;
                    currentRender = {};
                    currentRender.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);

                    Point pt{ static_cast<float>(pressed->position.x),
                              static_cast<float>(pressed->position.y) };
                    currentStroke.points.push_back(pt);
                    AppendPoint(currentRender, pt, currentStroke);
                }
            }

            if (const auto* moved = event->getIf<sf::Event::MouseMoved>()) {
                if (isDrawing) {
                    Point pt{ static_cast<float>(moved->position.x),
                              static_cast<float>(moved->position.y) };

                    // Distance check — skip if too close to last point
                    const auto& last = currentStroke.points.back();
                    float dx = pt.x - last.x;
                    float dy = pt.y - last.y;
                    if (dx * dx + dy * dy < 4.f) continue;

                    currentStroke.points.push_back(pt);
                    AppendPoint(currentRender, pt, currentStroke);
                }
            }

            if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>()) {
                if (released->button == sf::Mouse::Button::Left && isDrawing) {
                    isDrawing = false;
                    drawing.strokes.push_back(std::move(currentRender));
                    currentRender = {};
                    currentRender.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);
                }
            }
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

        // Draw completed strokes
        DrawDrawing(window, drawing);

        // Draw stroke in progress
        if (isDrawing) {
            window.draw(currentRender.quads);
            for (const auto& joint : currentRender.joints)
                window.draw(joint);
        }

        // Update the window
        window.display();
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
