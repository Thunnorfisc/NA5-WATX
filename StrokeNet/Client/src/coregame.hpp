#pragma once
#include "shared_protocol.hpp"
#include "state.hpp"
#include "Drawing.hpp"
#include "Canvas.hpp"
#include "ColourPicker.hpp"
#include "client.hpp"
#include <optional>

class StateMachine;

class CoreGameState final : public State
{
public:
    CoreGameState(StateMachine& stateMachine, StateContext& context);
    ~CoreGameState();
    void handleEvent(const sf::Event& event) override;
    void update(sf::Time deltaTime) override;
    void render() override;

private:
    [[nodiscard]] bool isMouseOverBackButton() const;
    void updateLayout();
    void handleCanvasStateCommandEvent(const CanvasDrawState& cds);
    Client::RegCanvasStateFnId _canvasStateFnId;

    sf::CircleShape m_backButton;
    sf::Font m_font;
    sf::Text m_titleText;
    sf::Text m_backText;
    InputState m_inputState;
    bool m_shouldReturnToMenu = false;
    bool m_leftDown = false;
    bool m_wasLeftDown = false;
    bool m_drawing = false;
    sf::Vector2f m_lastMousePos;

    Canvas m_canvas;
    ColourPicker m_cpicker;
    // For networking
    SequenceNumber sequenceNumber = 1;
};
