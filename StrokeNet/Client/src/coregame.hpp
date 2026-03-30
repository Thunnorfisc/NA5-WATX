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
#include "ToolPicker.hpp"
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
    [[nodiscard]] bool isMouseOverClearButton() const;
    void updateLayout();
    void setupTools();

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

    ToolPicker m_toolPicker;
    float m_brushThickness = 10.f; // Laze

    // SLIDER BULLSHIT
    bool m_showThicknessSlider = false;
    bool m_draggingSlider = false;
    sf::RectangleShape m_sliderTrack;
    sf::RectangleShape m_sliderKnob;
    sf::Text m_sliderValueText;
    static constexpr float SLIDER_MIN = 1.f;
    static constexpr float SLIDER_MAX = 50.f;
    static constexpr float SLIDER_WIDTH = 180.f;
    static constexpr float SLIDER_TRACK_H = 6.f;
    static constexpr float SLIDER_KNOB_W = 12.f;
    static constexpr float SLIDER_KNOB_H = 20.f;


    // PREVIEW SHIT
    sf::CircleShape m_cursorPreview;
    bool m_cursorOnCanvas = false;

    // CLEAR BUTTON
    sf::RectangleShape m_clearButton;
    sf::Text m_clearText;
};