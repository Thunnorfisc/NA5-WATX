#include "client.hpp"
#include "coregame.hpp"
#include "state_machine.hpp"

#include <iostream>
#include <algorithm>

#undef min // stupid microsoft
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
    m_titleText(m_font, "Core Game", 32),
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

    m_canvas = Canvas(sf::FloatRect({ 50.f, 50.f }, { 300.f, 300.f }));

    updateLayout();

    _canvasStateFnId = Client::registerCanvasStateCommandEvent(
        [this](const CanvasDrawState& cds) { handleCanvasStateCommandEvent(cds); });
}

CoreGameState::~CoreGameState()
{
    Client::deregisterCanvasStateCommandEvent(_canvasStateFnId);
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

            if (isMouseOverBackButton()) {
                m_shouldReturnToMenu = true;
                return;
            }
        }
    }

    for (int i = 0; i < 26; ++i) {
        if (const auto* keyboardPressed = event.getIf<sf::Event::KeyPressed>()) {
            if (keyboardPressed->code == sf::Keyboard::Key(i)) {
                std::cout << "Key " << char(i + 65) << " pressed" << std::endl;

                return;
			}
        }
	}


}

void CoreGameState::update(sf::Time)
{
    // create a new input state and send over to the server
    InputState is;
    // handle mouse position
    auto [mx, my] = sf::Mouse::getPosition(context().window);
    is.currentMousePos[0] = static_cast<MousePosition::value_type>(mx);
    is.currentMousePos[1] = static_cast<MousePosition::value_type>(my);
    is.currentSequenceNumber = sequenceNumber;
    Client::sendInputState(is);

    updateLayout();

    sf::Vector2i mousePos = sf::Mouse::getPosition(context().window);
    sf::Vector2f pos(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
    bool leftDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    if (leftDown && !m_wasLeftDown) {
        if (!m_cpicker.handleClick(pos) && m_canvas.contains(pos)) {
            auto mxHostOrder = static_cast<std::uint16_t>(mousePos.x);
            auto myHostOrder = static_cast<std::uint16_t>(mousePos.y);
            auto clr = m_cpicker.getSelectedColour();
            std::uint8_t thicknessHostOrder = 6;
            CanvasDrawState cds;
            cds._sqNumberHostOrder = sequenceNumber;
            cds._type = MessageType::PF_START_STROKE;
            std::vector<char> msg;
            msg.resize(13);
            std::uint32_t idHostOrder = m_canvas.nextId - 1;
            std::memcpy(msg.data(), &idHostOrder, sizeof(idHostOrder));
            std::memcpy(msg.data() + 4, &mxHostOrder, sizeof(mxHostOrder));
            std::memcpy(msg.data() + 6, &myHostOrder, sizeof(myHostOrder));
            std::memcpy(msg.data() + 8, &clr.r, sizeof(clr.r));
            std::memcpy(msg.data() + 9, &clr.g, sizeof(clr.g));
            std::memcpy(msg.data() + 10, &clr.b, sizeof(clr.b));
            std::memcpy(msg.data() + 11, &clr.a, sizeof(clr.a));
            std::memcpy(msg.data() + 12, &thicknessHostOrder, sizeof(thicknessHostOrder));
            cds._msg = std::move(msg);
            Client::sendCanvasCommand(cds);
            m_drawing = true;
        }
    }

    if (leftDown && m_drawing && pos != m_lastMousePos) {
        CanvasDrawState cds;
        cds._sqNumberHostOrder = sequenceNumber;
        cds._type = MessageType::PF_ADD_POINT;
        std::vector<char> msg;
        msg.resize(4);
        auto mxHostOrder = static_cast<std::uint16_t>(mousePos.x);
        auto myHostOrder = static_cast<std::uint16_t>(mousePos.y);
        std::memcpy(msg.data(), &mxHostOrder, sizeof(mxHostOrder));
        std::memcpy(msg.data() + 2, &myHostOrder, sizeof(myHostOrder)); // was +4, off by 2!
        cds._msg = std::move(msg);
        Client::sendCanvasCommand(cds);
    }

    if (!leftDown && m_wasLeftDown && m_drawing) {
        m_drawing = false;
        CanvasDrawState cds;
        cds._sqNumberHostOrder = sequenceNumber;
        cds._type = MessageType::PF_END_STROKE;
        cds._msg = {};
        Client::sendCanvasCommand(cds);
    }

    m_wasLeftDown = leftDown;
    m_lastMousePos = pos;

    // retrieve the stroke commands from the queue
    if (m_strokesMutex.try_lock())
    {
        std::queue<StrokeCmdReceived> copyCmds;
        copyCmds.swap(m_strokes);
        m_strokesMutex.unlock();

        while (!copyCmds.empty())
        {
            auto strokeCmd = copyCmds.front();
            copyCmds.pop();
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, BeginStroke>)
                {
                    m_canvas.beginStroke(value.mouse, value.color, value.thickness);
                }
                else if constexpr (std::is_same_v<T, AddPoint>)
                {
                    m_canvas.extendStroke(value.mouse);
                }
                else if constexpr (std::is_same_v<T, EndStroke>)
                {
                    m_canvas.endStroke();
                }
                else assert(false && "Missing visit case in std::visit in coregame");
                }, strokeCmd);
        }
    }


    if (m_shouldReturnToMenu)
    {
        m_shouldReturnToMenu = false;
        requestStateChange(StateId::MainMenu);
    }
    sequenceNumber++;
}

void CoreGameState::render()
{
    auto& window = context().window;
    window.draw(m_titleText);
    window.draw(m_backButton);
    window.draw(m_backText);
    m_canvas.draw(window);
    m_cpicker.draw(window);
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

    float pickerWidth = ColourPicker::COLS * (ColourPicker::SWATCH_SIZE + ColourPicker::PADDING) - ColourPicker::PADDING;
    m_cpicker.setPosition({
        m_canvas.bounds.position.x + (m_canvas.bounds.size.x - pickerWidth) / 2.f,
        m_canvas.bounds.position.y + m_canvas.bounds.size.y + 10.f
        });
}

void CoreGameState::handleCanvasStateCommandEvent(const CanvasDrawState& cds)
{
    // NOTE: ALL DATA SENT BY THE CLIENT.HPP IS ALREADY IN HOST ORDER!!!!!!!!!!
    switch (cds._type)
    {
        using enum MessageType;
    case PF_START_STROKE:
    {
        assert((cds._msg.size() == PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE) &&
            "Size of msg for PF_START_STROKE is wrong");

        ByteReader rdr{ .buffer = cds._msg };
        auto idHostOrder = rdr.read<std::uint32_t>(); // do nothing with this yet i guess
        auto mousePositionHostOrder = rdr.read<MousePosition>();
        auto RGBAT = rdr.read<std::array<char, 5>>();
        std::lock_guard lock(m_strokesMutex);
        m_strokes.push(BeginStroke{ sf::Vector2f(mousePositionHostOrder[0], mousePositionHostOrder[1]),
            sf::Color(RGBAT[0], RGBAT[1], RGBAT[2], RGBAT[3]), static_cast<float>(RGBAT[4])});
        break;
    }
    case PF_ADD_POINT:
    {
        assert((cds._msg.size() == PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE) &&
            "Size of msg for PF_ADD_POINT is wrong");

        ByteReader rdr{ .buffer = cds._msg };
        auto mousePositionHostOrder = rdr.read<MousePosition>();
        m_strokes.push(AddPoint{ sf::Vector2f(mousePositionHostOrder[0],mousePositionHostOrder[1])});
        break;
    }
    case PF_END_STROKE:
    {
        assert((cds._msg.size() == PacketSize::PF_END_STROKE - PacketSize::HEADER_SIZE) &&
            "Size of msg for PF_END_STROKE is wrong");
        m_strokes.push(EndStroke{});
        break;
    }
    default: assert(false && "Logic error");
    }
}
