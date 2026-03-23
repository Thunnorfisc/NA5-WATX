#include "loginstate.hpp"
#include "state_machine.hpp"
#include "client.hpp"

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
    m_font("resources/Cinzel-Regular.ttf"),
    m_titleText(m_font, "Login", 48),
    m_statusText(m_font, "", 20),
    m_loginButtonText(m_font, "Login", 28),
    m_createButtonText(m_font, "Create Account", 28)
{
    m_titleText.setFillColor(sf::Color(230, 230, 230));
    m_titleText.setOutlineThickness(2.0f);
    m_titleText.setOutlineColor(sf::Color(220, 70, 70));

    m_statusText.setFillColor(sf::Color::White);

    m_fieldValues[static_cast<int>(Field::Username)] = "";
    m_fieldValues[static_cast<int>(Field::Password)] = "";

    const std::array<std::string, FIELD_COUNT> labelStrings = {
        "Username:", "Password:"
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

    // login button
    m_loginButton.setSize({ 170.f, 50.f });
    m_loginButton.setFillColor(sf::Color(40, 120, 40));
    m_loginButton.setOutlineThickness(2.f);
    m_loginButton.setOutlineColor(sf::Color(60, 180, 60));
    m_loginButtonText.setFillColor(sf::Color(230, 230, 230));

    // create account button
    m_createButton.setSize({ 220.f, 50.f });
    m_createButton.setFillColor(sf::Color(40, 40, 120));
    m_createButton.setOutlineThickness(2.f);
    m_createButton.setOutlineColor(sf::Color(60, 60, 180));
    m_createButtonText.setFillColor(sf::Color(230, 230, 230));

    updateLayout();
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
            if (m_loginButton.getGlobalBounds().contains(pos))
            {
                attemptLogin();
                return;
            }
            if (m_createButton.getGlobalBounds().contains(pos))
            {
                attemptCreateAccount();
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
            std::size_t maxLen = (m_activeField == Field::Username)
                ? MAX_USERNAME_LEN - 1
                : MAX_PASSWORD_LEN - 1;

            if (m_fieldValues[idx].size() < maxLen)
                m_fieldValues[idx] += static_cast<char>(ch);
        }
    }
}

void LoginState::update(sf::Time)
{
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

    window.draw(m_loginButton);
    window.draw(m_loginButtonText);
    window.draw(m_createButton);
    window.draw(m_createButtonText);
    window.draw(m_statusText);
}

void LoginState::updateLayout()
{
    const sf::Vector2u windowSize = context().window.getSize();
    float centerX = windowSize.x * 0.5f;
    float startY = windowSize.y * 0.25f;

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

    float buttonY = fieldStartY + FIELD_COUNT * fieldSpacing + 20.f;
    float gap = 20.f;
    float totalButtonWidth = m_loginButton.getSize().x + m_createButton.getSize().x + gap;

    m_loginButton.setPosition({ centerX - totalButtonWidth * 0.5f, buttonY });
    centerTextHorizontally(m_loginButtonText,
        m_loginButton.getPosition().x + m_loginButton.getSize().x * 0.5f,
        buttonY + m_loginButton.getSize().y * 0.5f);

    m_createButton.setPosition({
        m_loginButton.getPosition().x + m_loginButton.getSize().x + gap, buttonY });
    centerTextHorizontally(m_createButtonText,
        m_createButton.getPosition().x + m_createButton.getSize().x * 0.5f,
        buttonY + m_createButton.getSize().y * 0.5f);

    float statusY = buttonY + 70.f;
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
    default:
        m_statusMessage = "Login failed (server unreachable or error)";
        m_statusColor = sf::Color(255, 100, 100);
        break;
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