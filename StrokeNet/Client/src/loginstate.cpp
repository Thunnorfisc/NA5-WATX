/* Start Header
***********************************************************************/

/*! \file   loginstate.cpp
    \author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

    /* End Header
    ***********************************************************************/
#include "client.hpp"
#include "loginstate.hpp"
#include "state_machine.hpp"
#include "shared_protocol.hpp"
#include <algorithm>
namespace
{
    void centerTextInBox(sf::Text& text, const sf::RectangleShape& box)
    {
        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin({
            bounds.position.x,
            bounds.position.y + bounds.size.y * 0.5f
            });
        text.setPosition({
            box.getPosition().x + 10.f,
            box.getPosition().y + box.getSize().y * 0.5f
            });
    }

    void centerTextHorizontally(sf::Text& text, float centerX, float y)
    {
        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin({
            bounds.position.x + bounds.size.x * 0.5f,
            bounds.position.y + bounds.size.y * 0.5f
            });
        text.setPosition({ centerX, y });
    }
}

LoginState::LoginState(StateMachine& stateMachine, StateContext& context) :
    State(stateMachine, context),
    m_font("resources/Marvel-Bold.ttf"),
    m_titleText(m_font, "Login", 48),
    m_statusText(m_font, "", 20),
    m_connectViaText(m_font, "Connect via:", 22),
    m_broadcastButtonText(m_font, "Auto Connect", 28),
    m_directButtonText(m_font, "Direct Connect", 28),
    m_newText(m_font, "New?", 22),
    m_createButtonText(m_font, "Create Account", 28),
    m_sound(m_hoverBuffer)
{
    m_sound.setVolume(100.0f);
    m_canPlayHover = m_hoverBuffer.loadFromFile("resources/UI_Hover_v1.wav");
    m_canPlayClick = m_clickBuffer.loadFromFile("resources/UI_Select_v1.wav");

    m_titleText.setFillColor(sf::Color(230, 230, 230));
    m_titleText.setOutlineThickness(2.0f);
    m_titleText.setOutlineColor(sf::Color(220, 70, 70));

    m_quitButton.setSize({ 200.f, 100.f });
    m_quitButton.setPosition({ 300.f, 200.f });
    m_quitButton.setFillColor(sf::Color::Blue);
    m_quitButton.setOutlineThickness(2.f);
    m_quitButton.setOutlineColor(sf::Color::White);

    m_statusText.setFillColor(sf::Color::White);

    for (int i = 0; i < FIELD_COUNT; ++i)
        m_fieldValues[i] = "";

    const std::array<std::string, FIELD_COUNT> labelStrings = {
        "Username:", "Password:", "Server IP Address:", /*"Port:"*/
    };

    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        m_fieldLabels[i].emplace(m_font, labelStrings[i], 22);
        m_fieldLabels[i]->setFillColor(sf::Color(200, 200, 200));

        m_fieldBoxes[i].setSize({ 350.f, 40.f });
        m_fieldBoxes[i].setFillColor(sf::Color(50, 50, 50));
        m_fieldBoxes[i].setOutlineThickness(2.f);
        m_fieldBoxes[i].setOutlineColor(sf::Color(100, 100, 100));

        m_fieldTexts[i].emplace(m_font, "", 22);
        m_fieldTexts[i]->setFillColor(sf::Color(230, 230, 230));
    }

    // highlight the active field
    m_fieldBoxes[static_cast<int>(m_activeField)].setOutlineColor(sf::Color(220, 70, 70));

    // "Connect via:" caption
    m_connectViaText.setFillColor(sf::Color(180, 180, 180));

    // broadcast button
    m_broadcastButton.setSize({ 170.f, 50.f });
    m_broadcastButton.setFillColor(sf::Color(40, 120, 40));
    m_broadcastButton.setOutlineThickness(2.f);
    m_broadcastButton.setOutlineColor(sf::Color(60, 180, 60));
    m_broadcastButtonText.setFillColor(sf::Color(230, 230, 230));

    // direct button
    m_directButton.setSize({ 170.f, 50.f });
    m_directButton.setFillColor(sf::Color(120, 80, 40));
    m_directButton.setOutlineThickness(2.f);
    m_directButton.setOutlineColor(sf::Color(180, 120, 60));
    m_directButtonText.setFillColor(sf::Color(230, 230, 230));

    // "New?" caption
    m_newText.setFillColor(sf::Color(180, 180, 180));

    // create account button
    m_createButton.setSize({ 220.f, 50.f });
    m_createButton.setFillColor(sf::Color(40, 40, 120));
    m_createButton.setOutlineThickness(2.f);
    m_createButton.setOutlineColor(sf::Color(60, 60, 180));
    m_createButtonText.setFillColor(sf::Color(230, 230, 230));

    updateLayout();
}

LoginState::~LoginState()
{
    m_sound.stop();
}

void LoginState::handleEvent(const sf::Event& event)
{
    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>())
    {
        if (mousePressed->button == sf::Mouse::Button::Left)
        {
            sf::Vector2f pos = context().window.mapPixelToCoords(
                sf::Vector2i(mousePressed->position.x, mousePressed->position.y));

            // check field clicks
            for (int i = 0; i < FIELD_COUNT; ++i)
            {
                if (m_fieldBoxes[i].getGlobalBounds().contains(pos))
                {
                    selectField(static_cast<Field>(i));
                    return;
                }
            }

            // check button clicks
            if (m_broadcastButton.getGlobalBounds().contains(pos))
            {
                if (m_canPlayClick)
                {
                    m_sound.stop();
                    m_sound.setBuffer(m_clickBuffer);
                    m_sound.play();
                }
                attemptLogin();
                return;
            }
            if (m_directButton.getGlobalBounds().contains(pos))
            {
                if (m_canPlayClick)
                {
                    m_sound.stop();
                    m_sound.setBuffer(m_clickBuffer);
                    m_sound.play();
                }
                
                attemptDirectConnect();
                return;
            }
            if (m_createButton.getGlobalBounds().contains(pos))
            {
                if (m_canPlayClick)
                {
                    m_sound.stop();
                    m_sound.setBuffer(m_clickBuffer);
                    m_sound.play();
                }
                attemptCreateAccount();
                return;
            }
            if (m_quitButton.getGlobalBounds().contains(pos))
            {
                if (m_canPlayClick)
                {
                    m_sound.stop();
                    m_sound.setBuffer(m_clickBuffer);
                    m_sound.play();
                }
                context().window.close();
                return;
            }
        }
    }

    if (const auto* textEntered = event.getIf<sf::Event::TextEntered>())
    {
        int idx = static_cast<int>(m_activeField);
        std::uint32_t ch = textEntered->unicode;

        if (ch == '\b') // backspace
        {
            if (!m_fieldValues[idx].empty())
                m_fieldValues[idx].pop_back();
        }
        else if (ch == '\t') // tab - move to next field
        {
            int next = (idx + 1) % FIELD_COUNT;
            selectField(static_cast<Field>(next));
        }
        else if (ch == '\r' || ch == '\n') // enter - attempt login
        {
            attemptLogin();
        }
        else if (ch >= 32 && ch < 127) // printable ASCII
        {
            std::size_t maxLen = 0;
            switch (m_activeField)
            {
            case Field::Username:  maxLen = MAX_USERNAME_LEN - 1; break;
            case Field::Password:  maxLen = MAX_PASSWORD_LEN - 1; break;
            case Field::ServerIP:  maxLen = 45;  break;
            //case Field::Port:      maxLen = 5;   break;
            default: break;
            }

            if (m_activeField == Field::ServerIP)
            {
                bool isDigit = (ch >= '0' && ch <= '9');
                bool isDot = (ch == '.');
                bool isColon = (ch == ':');
                bool isHex = (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
                if (!isDigit && !isDot && !isColon && !isHex)
                    return;
            }

            // for the port field, only allow digits
            //if (m_activeField == Field::Port && (ch < '0' || ch > '9'))
            //    return;

            if (maxLen > 0 && m_fieldValues[idx].size() < maxLen)
                m_fieldValues[idx] += static_cast<char>(ch);
        }
    }
}

void LoginState::update(sf::Time)
{
    sf::Vector2i ipos = sf::Mouse::getPosition(context().window);
    sf::Vector2f pos = context().window.mapPixelToCoords(ipos);
    m_isHoveringBroadcast = m_broadcastButton.getGlobalBounds().contains(pos);
    m_isHoveringDirect = m_directButton.getGlobalBounds().contains(pos);
    m_isHoveringCreate = m_createButton.getGlobalBounds().contains(pos);

    if (m_isHoveringBroadcast && !m_wasHoveringBroadcast)
    {
        if (m_canPlayHover)
        {
            m_sound.stop();
            m_sound.setBuffer(m_hoverBuffer);
            m_sound.play();
        }
    }
    if (m_isHoveringDirect && !m_wasHoveringDirect)
    {
        if (m_canPlayHover)
        {
            m_sound.stop();
            m_sound.setBuffer(m_hoverBuffer);
            m_sound.play();
        }
    }
    if (m_isHoveringCreate && !m_wasHoveringCreate)
    {
        if (m_canPlayHover)
        {
            m_sound.stop();
            m_sound.setBuffer(m_hoverBuffer);
            m_sound.play();
        }
    }
    if (m_isHoveringQuit && !m_wasHoveringQuit)
    {
        if (m_canPlayHover)
        {
            m_sound.stop();
            m_sound.setBuffer(m_hoverBuffer);
            m_sound.play();
        }
    }




    m_wasHoveringBroadcast = m_isHoveringBroadcast;
    m_wasHoveringDirect = m_isHoveringDirect;
    m_wasHoveringCreate = m_isHoveringCreate;
    m_wasHoveringQuit = m_isHoveringQuit;


    // update displayed text
    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        if (static_cast<Field>(i) == Field::Password)
        {
            m_fieldTexts[i]->setString(std::string(m_fieldValues[i].size(), '*'));
        }
        else
        {
            m_fieldTexts[i]->setString(m_fieldValues[i]);
        }
    }

    m_statusText.setString(m_statusMessage);
    m_statusText.setFillColor(m_statusColor);

    updateLayout();

    if (m_shouldTransition)
    {
        m_shouldTransition = false;
        requestStateChange(StateId::MainMenu);
    }
}

void LoginState::render()
{
    auto& window = context().window;

    window.draw(m_titleText);

    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        window.draw(m_fieldBoxes[i]);
        window.draw(*m_fieldLabels[i]);
        window.draw(*m_fieldTexts[i]);
    }

    window.draw(m_connectViaText);
    window.draw(m_broadcastButton);
    window.draw(m_broadcastButtonText);
    window.draw(m_directButton);
    window.draw(m_directButtonText);

    window.draw(m_newText);
    window.draw(m_createButton);
    window.draw(m_createButtonText);

    window.draw(m_statusText);
}

void LoginState::updateLayout()
{
    const sf::Vector2u windowSize = context().window.getSize();
    float centerX = windowSize.x * 0.5f;
    float startY = windowSize.y * 0.2f;

    centerTextHorizontally(m_titleText, centerX, startY);

    float fieldStartY = startY + 80.f;
    float fieldSpacing = 70.f;

    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        float y = fieldStartY + i * fieldSpacing;
        float boxX = centerX - m_fieldBoxes[i].getSize().x * 0.5f;

        m_fieldLabels[i]->setPosition({ boxX, y - 25.f });
        m_fieldBoxes[i].setPosition({ boxX, y });
        centerTextInBox(*m_fieldTexts[i], m_fieldBoxes[i]);
    }

    // --- "Connect via:" caption + Broadcast / Direct buttons ---
    float connectCaptionY = fieldStartY + FIELD_COUNT * fieldSpacing + 10.f;
    centerTextHorizontally(m_connectViaText, centerX, connectCaptionY);

    float connectButtonY = connectCaptionY + 25.f;
    float gap = 20.f;
    float connectTotalWidth = m_broadcastButton.getSize().x + m_directButton.getSize().x + gap;

    m_broadcastButton.setPosition({ centerX - connectTotalWidth * 0.5f, connectButtonY });
    centerTextHorizontally(m_broadcastButtonText,
        m_broadcastButton.getPosition().x + m_broadcastButton.getSize().x * 0.5f,
        connectButtonY + m_broadcastButton.getSize().y * 0.5f);

    m_directButton.setPosition({
        m_broadcastButton.getPosition().x + m_broadcastButton.getSize().x + gap, connectButtonY });
    centerTextHorizontally(m_directButtonText,
        m_directButton.getPosition().x + m_directButton.getSize().x * 0.5f,
        connectButtonY + m_directButton.getSize().y * 0.5f);

    // --- "New?" caption + Create Account button ---
    float newCaptionY = connectButtonY + m_broadcastButton.getSize().y + 25.f;
    centerTextHorizontally(m_newText, centerX, newCaptionY);

    float createButtonY = newCaptionY + 25.f;
    m_createButton.setPosition({
        centerX - m_createButton.getSize().x * 0.5f, createButtonY });
    centerTextHorizontally(m_createButtonText,
        centerX,
        createButtonY + m_createButton.getSize().y * 0.5f);

    // status text below everything
    float statusY = createButtonY + m_createButton.getSize().y + 25.f;
    centerTextHorizontally(m_statusText, centerX, statusY);
}

void LoginState::selectField(Field field)
{
    m_fieldBoxes[static_cast<int>(m_activeField)].setOutlineColor(sf::Color(100, 100, 100));
    m_activeField = field;
    m_fieldBoxes[static_cast<int>(m_activeField)].setOutlineColor(sf::Color(220, 70, 70));
}

void LoginState::attemptLogin()
{
    const auto& user = m_fieldValues[static_cast<int>(Field::Username)];
    const auto& pass = m_fieldValues[static_cast<int>(Field::Password)];

    if (user.empty() || pass.empty())
    {
        m_statusMessage = "Username and password are required";
        m_statusColor = sf::Color(255, 100, 100);
        return;
    }

    m_statusMessage = "Searching for server...";
    m_statusColor = sf::Color(200, 200, 100);

    LoginStatus result = Client::loginViaBroadcast(user, pass);

    switch (result)
    {
    case LoginStatus::SUCCESS:
        m_statusMessage = "Login successful!";
        m_statusColor = sf::Color(100, 255, 100);
        m_shouldTransition = true;
        break;
    case LoginStatus::INVALID_CREDENTIALS:
        m_statusMessage = "Invalid username or password";
        m_statusColor = sf::Color(255, 100, 100);
        break;
    case LoginStatus::ALREADY_LOGGED_IN:
        m_statusMessage = "This account is already logged in";
        m_statusColor = sf::Color(255, 100, 100);
        break;
    case LoginStatus::SERVER_NO_RESPONSE:
        m_statusMessage = "Login failed (server unreachable or error)";
        m_statusColor = sf::Color(255, 100, 100);
        break;
    default:
        m_statusMessage = "Login failed (server unreachable or error)";
        m_statusColor = sf::Color(255, 100, 100);
        break;
    }
}

void LoginState::attemptDirectConnect()
{
    const auto& user = m_fieldValues[static_cast<int>(Field::Username)];
    const auto& pass = m_fieldValues[static_cast<int>(Field::Password)];
    const auto& ip = m_fieldValues[static_cast<int>(Field::ServerIP)];
    //const auto& port = m_fieldValues[static_cast<int>(Field::Port)];

    if (user.empty() || pass.empty())
    {
        m_statusMessage = "Username and password are required";
        m_statusColor = sf::Color(255, 100, 100);
        return;
    }

    if (ip.empty() /*|| port.empty()*/)
    {
        m_statusMessage = "Server IP is required direct connect";
        m_statusColor = sf::Color(255, 100, 100);
        return;
    }

    // validate IP address format
    if (ip.find(':') == std::string::npos)
    {
        // IPv4 validation
        bool validIP = true;
        int dotCount = 0;
        int octetStart = 0;

        for (std::size_t i = 0; i <= ip.size(); ++i)
        {
            if (i == ip.size() || ip[i] == '.')
            {
                int octetLen = static_cast<int>(i) - octetStart;
                if (octetLen < 1 || octetLen > 3) { validIP = false; break; }

                int octet = 0;
                for (int j = octetStart; j < static_cast<int>(i); ++j)
                {
                    if (ip[j] < '0' || ip[j] > '9') { validIP = false; break; }
                    octet = octet * 10 + (ip[j] - '0');
                }
                if (!validIP || octet > 255) { validIP = false; break; }

                // reject leading zeros (e.g. "01", "001") except plain "0"
                if (octetLen > 1 && ip[octetStart] == '0') { validIP = false; break; }

                if (i < ip.size()) ++dotCount;
                octetStart = static_cast<int>(i) + 1;
            }
        }

        if (dotCount != 3) validIP = false;

        if (!validIP)
        {
            m_statusMessage = "Invalid IPv4 address (e.g. 192.168.1.1)";
            m_statusColor = sf::Color(255, 100, 100);
            return;
        }
    }

    // validate port is a valid number in range
    //int portNum = 0;
    //try { portNum = std::stoi(port); }
    //catch (...)
    //{
    //    m_statusMessage = "Invalid port number";
    //    m_statusColor = sf::Color(255, 100, 100);
    //    return;
    //}

    //if (portNum < 1 || portNum > 65535)
    //{
    //    m_statusMessage = "Port must be between 1 and 65535";
    //    m_statusColor = sf::Color(255, 100, 100);
    //    return;
    //}

    m_statusMessage = "Connecting to " + ip + ":" + std::to_string(ServerUdpPort) + "...";
    m_statusColor = sf::Color(200, 200, 100);

    //// TODO: TIMMMMMMMMMMMMMMMMMMMMMMMMM/NICHTSSSSSSS HERE

    //// TODO: placeholder until direct connect is implemented
    //m_statusMessage = "Direct connect not yet implemented";
    //m_statusColor = sf::Color(255, 200, 100);

    // Ok i hear u thunderfishy boi :3

    LoginStatus status = Client::loginViaIp(user, pass, ip);
    switch (status)
    {
        using enum LoginStatus;
    case SUCCESS: m_statusMessage = "Login Successful!"; m_statusColor = sf::Color(100, 255, 100); m_shouldTransition = true; break;
    default: m_statusColor = sf::Color(255, 100, 100); [[fallthrough]];
    case INVALID_CREDENTIALS: m_statusMessage = "Invalid username or password"; break;
    case ALREADY_LOGGED_IN: m_statusMessage = "This account is already logged in"; break;
    case SERVER_NO_RESPONSE: m_statusMessage = "Login failed (server unreachable or error)"; break;
    }
}

void LoginState::attemptCreateAccount()
{
    const auto& user = m_fieldValues[static_cast<int>(Field::Username)];
    const auto& pass = m_fieldValues[static_cast<int>(Field::Password)];

    if (user.empty() || pass.empty())
    {
        m_statusMessage = "Username and password are required";
        m_statusColor = sf::Color(255, 100, 100);
        return;
    }

    if (user.size() >= MAX_USERNAME_LEN)
    {
        m_statusMessage = "Username too long";
        m_statusColor = sf::Color(255, 100, 100);
        return;
    }

    m_statusMessage = "Searching for server...";
    m_statusColor = sf::Color(200, 200, 100);

    LoginStatus result = Client::createAccountViaBroadcast(user, pass);

    switch (result)
    {
    case LoginStatus::SUCCESS:
        m_statusMessage = "Account created! You can now log in.";
        m_statusColor = sf::Color(100, 255, 100);
        break;
    case LoginStatus::USERNAME_TAKEN:
        m_statusMessage = "Username already taken";
        m_statusColor = sf::Color(255, 100, 100);
        break;
    default:
        m_statusMessage = "Account creation failed";
        m_statusColor = sf::Color(255, 100, 100);
        break;
    }
}