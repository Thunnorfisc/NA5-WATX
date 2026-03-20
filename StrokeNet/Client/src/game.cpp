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
    {
        // Process events
        while (const std::optional event = window.pollEvent())
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
        }

        // Clear screen
        window.clear();

        // Draw the sprite
        window.draw(sprite);

        // Draw the string
        window.draw(text);

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
    }
}