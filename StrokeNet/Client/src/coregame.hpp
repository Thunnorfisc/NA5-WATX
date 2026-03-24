#pragma once
#include "shared_protocol.hpp"
#include "state.hpp"
#include "Drawing.hpp"
#include "Canvas.hpp"
#include "ColourPicker.hpp"
#include "client.hpp"
#include "chatBox.hpp"
#include <mutex>
#include <queue>
#include <variant>
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
    void handleCanvasStateCommandEvent(const CanvasDrawCommand& cds);
    void handleChatMessageEvent(const ReceivedChatMessage& chatMsg);

    Client::RegCanvasStateFnId _canvasStateFnId;
    Client::RegChatMessageFnId _chatMessageFnId;

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

    struct BeginStroke { sf::Vector2f mouse; sf::Color color; float thickness; };
    struct AddPoint { sf::Vector2f mouse; };
    struct EndStroke {};
    struct ReceivedMsg { std::string playerName; std::string msg };

    using CmdReceived = std::variant<
        BeginStroke,
        AddPoint,
        EndStroke,
        ReceivedMsg>;
    std::queue<CmdReceived> m_commands;
    std::mutex m_commandsMutex;

    Canvas m_canvas;
    ChatBox m_chatBox;

    ColourPicker m_cpicker;
    // For networking
    SequenceNumber sequenceNumber = 1;
};
