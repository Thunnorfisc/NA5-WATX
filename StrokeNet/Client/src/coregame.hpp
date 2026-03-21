#pragma once
#include "shared_protocol.hpp"
#include "state.hpp"
#include "Drawing.hpp"
#include "Canvas.hpp"
#include <optional>

class StateMachine;

class CoreGameState final : public State
{
public:
    CoreGameState(StateMachine& stateMachine, StateContext& context);

    void handleEvent(const sf::Event& event) override;
    void update(sf::Time deltaTime) override;
    void render() override;

private:
    [[nodiscard]] bool isMouseOverBackButton() const;
    void updateLayout();

    sf::CircleShape m_backButton;
    sf::Font m_font;
    sf::Text m_titleText;
    sf::Text m_backText;
    std::optional<GameState> m_gameState;
    InputState m_inputState;
    bool m_shouldReturnToMenu = false;

    Canvas m_canvas;
    // THESE ARE TO BE REFACTORED
    Drawing drawing;
    Stroke currentStroke;
    RenderStroke currentRender;
    bool isDrawing = false;
    uint32_t nextId = 1;

    // For networking
    SequenceNumber sequenceNumber = 1;
};
