/* Start Header
***********************************************************************/

/*! \file   coregame.cpp
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
#include "client.hpp"
#include "coregame.hpp"
#include "state_machine.hpp"

#include <chrono>
#include <iostream>
#include <algorithm>

#undef min // stupid microsoft
#undef max // MICROSOFTTTTTTTTT
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
    m_font("resources/Marvel-Bold.ttf"),
    m_titleText(m_font, "", 32), // was "Core Game" -imma remove it cus idk what it's for -snowpuppy
    m_backText(m_font, "Main Menu", 34),
    m_chatBox("resources/Marvel-Regular.ttf")
{
	m_backButton.setPosition({ 10, 830 });
	m_backButton.setSize({ 150, 50 });
    m_backButton.setFillColor(sf::Color(45, 45, 45));
    m_backButton.setOutlineThickness(5.0f);
    m_backButton.setOutlineColor(sf::Color(220, 70, 70));

    m_titleText.setFillColor(sf::Color(230, 230, 230));
    m_titleText.setOutlineThickness(2.0f);
    m_titleText.setOutlineColor(sf::Color(220, 70, 70));

    m_backText.setFillColor(sf::Color(230, 230, 230));

    m_canvas = Canvas(sf::FloatRect({ 50.f, 50.f }, { 300.f, 300.f }));

    setupTools();
    updateLayout();
}

CoreGameState::~CoreGameState()
{
}

void CoreGameState::handleEvent(const sf::Event& event)
{
    updateLayout();

    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        sf::FloatRect visibleArea({ 0.f, 0.f },
            { static_cast<float>(resized->size.x), static_cast<float>(resized->size.y) });
        context().window.setView(sf::View(visibleArea));
    }

    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mousePressed->button == sf::Mouse::Button::Left) {
            sf::Vector2f pos(static_cast<float>(mousePressed->position.x),
                static_cast<float>(mousePressed->position.y));

            if (isMouseOverBackButton() && Client::quitGame()) {
                m_shouldReturnToMenu = true;
                return;
            }
            else if (isMouseOverTextBox()) {
                m_chatBox.setTyping(true);
                return;
			}
        }
    }

    if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        if (keyPressed->code == sf::Keyboard::Key::E && !m_chatBox.m_isTyping) {
            m_canvas.eraseMode = true;
        }
        if (keyPressed->code == sf::Keyboard::Key::R && !m_chatBox.m_isTyping) {
            m_canvas.eraseMode = false;
        }
    }

    if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        if (keyPressed->code == sf::Keyboard::Key::C && !m_chatBox.m_isTyping) {
            Client::sendClearCanvas(m_canvas.clearId);
        }
    }
    if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        if (keyPressed->code == sf::Keyboard::Key::Enter && !m_chatBox.m_isTyping) {
            m_chatBox.setTyping(true);
            return;
        }
	}
    m_chatBox.handleEvent(event);
}

void CoreGameState::update(sf::Time)
{
    updateLayout();

    sf::Vector2i mousePos = sf::Mouse::getPosition(context().window);
    sf::Vector2f pos(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
    bool leftDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    if (leftDown && !m_wasLeftDown) {
        // Check tool picker first, then colour picker, then canvas
        if (!m_toolPicker.handleClick(pos) &&
            !m_cpicker.handleClick(pos) &&
            m_canvas.contains(pos))
        { // < start stroke
            m_drawing = true;
            auto clr = m_cpicker.getSelectedColour();
            Client::sendStartStroke(m_canvas.nextId, // < stroke id
                std::array<std::uint16_t, 2>{ // < mouse pos
                static_cast<std::uint16_t>(mousePos.x),
                    static_cast<std::uint16_t>(mousePos.y)
            },
                std::array<std::uint8_t, 5>{ // < colour
                clr.r,
                    clr.g,
                    clr.b,
                    clr.a,
                    static_cast<std::uint8_t>(m_brushThickness)
            });
        }
    }

    if (leftDown && m_drawing && pos != m_lastMousePos) { // < extend stroke
        Client::sendExtendStroke(m_canvas.nextId, // < stroke id
            std::array<std::uint16_t, 2>{ // < mouse pos
            static_cast<std::uint16_t>(mousePos.x),
                static_cast<std::uint16_t>(mousePos.y)
        });
    }

    if (!leftDown && m_wasLeftDown && m_drawing) {
        m_drawing = false;
        Client::sendEndStroke(m_canvas.nextId);
        m_canvas.nextId++;
    }

    m_wasLeftDown = leftDown;
    m_lastMousePos = pos;

    m_cursorOnCanvas = m_canvas.contains(pos) && !m_drawing;
    if (m_cursorOnCanvas) {
        float radius = m_brushThickness / 2.f;
        m_cursorPreview.setRadius(radius);
        m_cursorPreview.setOrigin({ radius, radius });
        m_cursorPreview.setPosition(pos);
        m_cursorPreview.setPointCount(40);

        if (m_canvas.eraseMode) {
            // Eraser: hollow white circle with dashed-style outline
            m_cursorPreview.setFillColor(sf::Color::Transparent);
            m_cursorPreview.setOutlineColor(sf::Color(120, 120, 120));
            m_cursorPreview.setOutlineThickness(1.5f);
        }
        else {
            // Drawing: filled circle with selected colour at half opacity
            auto clr = m_cpicker.getSelectedColour();
            m_cursorPreview.setFillColor(sf::Color(clr.r, clr.g, clr.b, 128));
            m_cursorPreview.setOutlineColor(sf::Color(clr.r, clr.g, clr.b, 200));
            m_cursorPreview.setOutlineThickness(1.f);
        }
    }

    handle_received_chatMessages();
    handle_received_strokeCommands();
    handle_received_strokeHistory();
    handle_received_msgHistory();

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
    m_canvas.draw(window);
    m_cpicker.draw(window);
    m_toolPicker.draw(window);
	m_chatBox.draw(window);

    std::int64_t retMs = Client::getRoundEndTimeMs();
    
    std::int64_t nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::steady_clock::now().time_since_epoch()).count();

    std::int64_t timeRemainingMs = retMs - nowMs;
    if(timeRemainingMs < std::int64_t{0}) timeRemainingMs = std::int64_t{0};

    m_chatBox.displayTimer(timeRemainingMs / 1000);

    if (!Client::getWord().empty()) {
        sf::Text wordText(m_font, Client::getWord(), 28);
        wordText.setFillColor(sf::Color(0, 255, 255));
        wordText.setOutlineThickness(2.0f);
        wordText.setOutlineColor(sf::Color(220, 70, 70));
		wordText.setPosition({ 400.f, 20.f });
#ifdef _DEBUG
		//std::cout << "Word:" << wordText.getString().toAnsiString() << std::endl;
#endif
		window.draw(wordText);
    }
    else {
		auto wordLen = Client::getWordLength();
		sf::Text wordHintText(m_font, std::string(wordLen, '-') + " ", 28);
        wordHintText.setFillColor(sf::Color(0, 255, 255));
        wordHintText.setOutlineThickness(2.0f);
        wordHintText.setOutlineColor(sf::Color(220, 70, 70));
		wordHintText.setPosition({ 400.f, 20.f + wordHintText.getLocalBounds().size.y }); // seems to not be able to display underscore '_' so i use dash and lower the height to make it look like an underscore uwu
#ifdef _DEBUG
		//std::cout << "Word Hint:" << wordHintText.getString().toAnsiString() << std::endl;
#endif
		window.draw(wordHintText);
    }

    if (m_cursorOnCanvas) {
        window.draw(m_cursorPreview);
    }
}

bool CoreGameState::isMouseOverBackButton() const
{
    const sf::Vector2i pixelPosition = sf::Mouse::getPosition(context().window);
    const sf::Vector2f worldPosition = context().window.mapPixelToCoords(pixelPosition);
    return m_backButton.getGlobalBounds().contains(worldPosition);
}

bool CoreGameState::isMouseOverTextBox() const 
{
    const sf::Vector2i pixelPosition = sf::Mouse::getPosition(context().window);
    const sf::Vector2f worldPosition = context().window.mapPixelToCoords(pixelPosition);
    return m_chatBox.textTypingArea.getGlobalBounds().contains(worldPosition);
}

void CoreGameState::updateLayout()
{
    const sf::Vector2u windowSize = context().window.getSize();
    const float scale = std::min(windowSize.x / 1600.0f, windowSize.y / 900.0f);

    centerText(m_titleText, { windowSize.x * 0.5f, windowSize.y * 0.05f });

    const sf::Vector2f backButtonPosition(windowSize.x * 0.08f, windowSize.y * 0.9f);
    m_backButton.setScale({ scale, scale });
    m_backButton.setPosition(backButtonPosition);

    centerText(m_backText, m_backButton.getGlobalBounds().getCenter());

    sf::Vector2f canvasSize(900.f, 500.f);
    sf::Vector2f canvasPos(
        (windowSize.x - canvasSize.x) / 2.f,
        (windowSize.y - canvasSize.y) / 2.f
    );
    m_canvas.bounds = sf::FloatRect(canvasPos, canvasSize);
    m_canvas.border.setPosition(canvasPos);
    m_canvas.border.setSize(canvasSize);

    float belowCanvas = m_canvas.bounds.position.y + m_canvas.bounds.size.y + 10.f;

    float maxToolsInGroup = 0.f;
    for (const auto& g : m_toolPicker.groups) {
        maxToolsInGroup = std::max(maxToolsInGroup, static_cast<float>(g.tools.size()));
    }
    float toolPickerWidth = maxToolsInGroup * (ToolPicker::TOOL_SIZE + ToolPicker::PADDING) - ToolPicker::PADDING;
    m_toolPicker.setPosition({
        m_canvas.bounds.position.x,
        belowCanvas
        });

    float pickerWidth = ColourPicker::COLS * (ColourPicker::SWATCH_SIZE + ColourPicker::PADDING) - ColourPicker::PADDING;

    float belowTools = belowCanvas + m_toolPicker.getTotalHeight() + 10.f;
    m_cpicker.setPosition({
        m_canvas.bounds.position.x + (m_canvas.bounds.size.x - pickerWidth) / 2.f,
        belowTools
        });
}

void CoreGameState::setupTools()
{
    // === Main tools group (mutually exclusive: draw vs erase) ===
    int mainGroup = m_toolPicker.addGroup();

    m_toolPicker.addTool(mainGroup, "Pencil", "resources/sprites/drawing_pencil.png",
        [this]() {
            m_canvas.eraseMode = false;
        });

    m_toolPicker.addTool(mainGroup, "Eraser", "resources/sprites/drawing_eraser.png",
        [this]() {
            m_canvas.eraseMode = true;
        });

    // === Helper tools group (mutually exclusive: brush sizes) ===
    int helperGroup = m_toolPicker.addGroup();

    m_toolPicker.addTool(helperGroup, "Thin", "resources/sprites/dot_small.png",
        [this]() {
            m_brushThickness = 10.f;
        });

    m_toolPicker.addTool(helperGroup, "Thick", "resources/sprites/dot_large.png",
        [this]() {
            m_brushThickness = 30.f;
        });

    // Select defaults: Pencil (group 0, tool 0) and Thin (group 1, tool 0)
    m_toolPicker.select(mainGroup, 0);
    m_toolPicker.select(helperGroup, 0);
}

void CoreGameState::handle_received_strokeCommands()
{
    auto strokeCommands = Client::getReceivedStrokeCommands();
    while (!strokeCommands.empty())
    {
        auto scmd = strokeCommands.front();
        strokeCommands.pop();
        ByteReader rdr{ .buffer = scmd._data };
        switch (scmd._type)
        {
            using enum Client::ReceivedStrokeCommand::Type;
        case START_STROKE:
        {
            auto mousePos = rdr.read<MousePosition>();
            auto rgbat = rdr.read<std::array<std::uint8_t, 5>>();
            m_canvas.beginStroke(
                sf::Vector2f{
                    static_cast<float>(mousePos[0]),
                    static_cast<float>(mousePos[1])
                },
                sf::Color{
                    static_cast<std::uint8_t>(rgbat[0]),
                    static_cast<std::uint8_t>(rgbat[1]),
                    static_cast<std::uint8_t>(rgbat[2]),
                    static_cast<std::uint8_t>(rgbat[3])
                },
                static_cast<float>(rgbat[4])
            );
            break;
        }
        case EXTEND_STROKE:
        {
            auto mousePos = rdr.read<MousePosition>();
            m_canvas.extendStroke(
                sf::Vector2f{
                    static_cast<float>(mousePos[0]),
                    static_cast<float>(mousePos[1])
                });
            break;
        }
        case END_STROKE:
        {
            m_canvas.endStroke();
            break;
        }
        case CLEAR_CANVAS:
        {
            m_canvas.clear();
            break;
        }
        }
    }
}

void CoreGameState::handle_received_chatMessages()
{
    auto chatMsges = Client::getReceivedChatMessages();
    while (!chatMsges.empty())
    {
        auto chatmsg = chatMsges.front();
        chatMsges.pop();
        m_chatBox.receiveMessageFromServer(chatmsg._name, chatmsg._message);
    }
}

void CoreGameState::handle_received_strokeHistory()
{
    auto strokeHistory = Client::getStrokeHistory();
    if (strokeHistory)
    {
        // there is a stroke history for us to interpret
        m_canvas.clear();

        for (const auto& strokeCmd : strokeHistory->_strokeHistory)
        {
            ByteReader rdr{ .buffer = strokeCmd._data };
            switch (strokeCmd._type)
            {
                using enum PastStroke::Type;
            case START_STROKE:
            {
                auto mousePos = rdr.read<MousePosition>();
                auto rgbat = rdr.read<std::array<std::uint8_t, 5>>();
                m_canvas.beginStroke(
                    sf::Vector2f{
                        static_cast<float>(mousePos[0]),
                        static_cast<float>(mousePos[1])
                    },
                    sf::Color{
                        static_cast<std::uint8_t>(rgbat[0]),
                        static_cast<std::uint8_t>(rgbat[1]),
                        static_cast<std::uint8_t>(rgbat[2]),
                        static_cast<std::uint8_t>(rgbat[3])
                    },
                    static_cast<float>(rgbat[4])
                );
                break;
            }
            case EXTEND_STROKE:
            {
                auto mousePos = rdr.read<MousePosition>();
                m_canvas.extendStroke(
                    sf::Vector2f{
                        static_cast<float>(mousePos[0]),
                        static_cast<float>(mousePos[1])
                    });
                break;
            }
            case END_STROKE:
            {
                m_canvas.endStroke();
                break;
            }
            }
        }
    }
}

void CoreGameState::handle_received_msgHistory()
{
    auto msgHistory = Client::getMessageHistory();
    if (msgHistory)
    {
        m_chatBox.clearChatHistory();
        for (const auto& msg : msgHistory->_chatMessageHistory)
        {
            m_chatBox.receiveMessageFromServer(msg._name, msg._message);
        }
    }
}
