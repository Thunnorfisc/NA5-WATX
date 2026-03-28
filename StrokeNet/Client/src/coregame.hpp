/* Start Header
***********************************************************************/

/*! \file   coregame.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \co-author Alfred Lo Kai Xuan
    \par    email: alfredkaixuan.lo@digipen.edu
    \co-author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   25th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
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
	[[nodiscard]] bool isMouseOverTextBox() const;
    void updateLayout();

    void handle_received_strokeCommands();
    void handle_received_chatMessages();
    void handle_received_strokeHistory();
    void handle_received_msgHistory();

    sf::RectangleShape m_backButton;
    sf::Font m_font;
    sf::Text m_titleText;
    sf::Text m_backText;
    bool m_shouldReturnToMenu = false;
    bool m_leftDown = false;
    bool m_wasLeftDown = false;
    bool m_drawing = false;
    sf::Vector2f m_lastMousePos;

    Canvas m_canvas;
    ChatBox m_chatBox;

    ColourPicker m_cpicker;
};
