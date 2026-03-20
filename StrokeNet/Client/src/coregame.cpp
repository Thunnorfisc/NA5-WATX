#include "coregame.hpp"

#include "state_machine.hpp"

#include <algorithm>

namespace
{
    void centerText(sf::Text& text, sf::Vector2f position)
    {
        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin({
            bounds.position.x + bounds.size.x * 0.5f,
            bounds.position.y + bounds.size.y * 0.5f
            });
        text.setPosition(position);
    }
}

CoreGameState::CoreGameState(StateMachine& stateMachine, StateContext& context) :
    State(stateMachine, context),
    m_font("resources/Cinzel-Regular.ttf"),
    m_titleText(m_font, "Core Game", 64),
    m_backText(m_font, "Main Menu", 34)
{
    m_backButton.setRadius(110.0f);
    m_backButton.setOrigin({ 110.0f, 110.0f });
    m_backButton.setFillColor(sf::Color(45, 45, 45));
    m_backButton.setOutlineThickness(5.0f);
    m_backButton.setOutlineColor(sf::Color(220, 70, 70));

    m_titleText.setFillColor(sf::Color(230, 230, 230));
    m_titleText.setOutlineThickness(2.0f);
    m_titleText.setOutlineColor(sf::Color(220, 70, 70));

    m_backText.setFillColor(sf::Color(230, 230, 230));

    updateLayout();
}

void CoreGameState::handleEvent(const sf::Event& event)
{
    updateLayout();

    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>())
    {
        if (mousePressed->button == sf::Mouse::Button::Left && isMouseOverBackButton())
        {
            m_shouldReturnToMenu = true;
        }
    }
    if (const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
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

    if (const auto* moved = event.getIf<sf::Event::MouseMoved>()) {
        if (isDrawing) {
            Point pt{ static_cast<float>(moved->position.x),
                        static_cast<float>(moved->position.y) };

            // Distance check — skip if too close to last point
            const auto& last = currentStroke.points.back();
            float dx = pt.x - last.x;
            float dy = pt.y - last.y;
            if (dx * dx + dy * dy < 4.f) return;

            currentStroke.points.push_back(pt);
            AppendPoint(currentRender, pt, currentStroke);
        }
    }

    if (const auto* released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (released->button == sf::Mouse::Button::Left && isDrawing) {
            isDrawing = false;
            drawing.strokes.push_back(std::move(currentRender));
            currentRender = {};
            currentRender.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);
        }
    }
}

void CoreGameState::update(sf::Time)
{
    m_gameState = tryGetGameState();
    sendInputState(m_inputState);

    updateLayout();

    if (m_shouldReturnToMenu)
    {
        m_shouldReturnToMenu = false;
        requestStateChange(StateId::MainMenu);
    }
}

void CoreGameState::render()
{
    auto& window = context().window;
    window.draw(m_titleText);
    window.draw(m_backButton);
    window.draw(m_backText);
    // Draw completed strokes
    DrawDrawing(window, drawing);

    // Draw stroke in progress
    if (isDrawing) {
        window.draw(currentRender.quads);
        for (const auto& joint : currentRender.joints)
            window.draw(joint);
    }
}

bool CoreGameState::isMouseOverBackButton() const
{
    const sf::Vector2i pixelPosition = sf::Mouse::getPosition(context().window);
    const sf::Vector2f worldPosition = context().window.mapPixelToCoords(pixelPosition);
    return m_backButton.getGlobalBounds().contains(worldPosition);
}

void CoreGameState::updateLayout()
{
    const sf::Vector2u windowSize = context().window.getSize();
    const float scale = std::min(windowSize.x / 1600.0f, windowSize.y / 900.0f);

    centerText(m_titleText, { windowSize.x * 0.5f, windowSize.y * 0.3f });

    const sf::Vector2f backButtonPosition(windowSize.x * 0.18f, windowSize.y * 0.72f);
    m_backButton.setScale({ scale, scale });
    m_backButton.setPosition(backButtonPosition);

    centerText(m_backText, m_backButton.getGlobalBounds().getCenter());
}
