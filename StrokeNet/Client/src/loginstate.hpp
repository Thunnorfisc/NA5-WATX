#pragma once

#include "state.hpp"
#include "shared_protocol.hpp"

#include <string>
#include <array>
#include <optional>

class StateMachine;

class LoginState final : public State
{
public:
    LoginState(StateMachine& stateMachine, StateContext& context);

    void handleEvent(const sf::Event& event) override;
    void update(sf::Time deltaTime) override;
    void render() override;

private:
    enum class Field { Username, Password, COUNT };
    static constexpr int FIELD_COUNT = static_cast<int>(Field::COUNT);

    void updateLayout();
    void selectField(Field field);
    void attemptLogin();
    void attemptCreateAccount();

    sf::Font m_font;
    sf::Text m_titleText;
    sf::Text m_statusText;

    // input fields
    std::array<std::string, FIELD_COUNT> m_fieldValues;
    std::array<std::optional<sf::Text>, FIELD_COUNT> m_fieldLabels;
    std::array<sf::RectangleShape, FIELD_COUNT> m_fieldBoxes;
    std::array<std::optional<sf::Text>, FIELD_COUNT> m_fieldTexts;
    Field m_activeField = Field::Username;

    // buttons
    sf::RectangleShape m_loginButton;
    sf::Text m_loginButtonText;
    sf::RectangleShape m_createButton;
    sf::Text m_createButtonText;

    std::string m_statusMessage;
    sf::Color m_statusColor = sf::Color::White;
    bool m_shouldTransition = false;
};