#pragma once

#include "state.hpp"

class StateMachine;

class MainMenuState final : public State
{
public:
    MainMenuState(StateMachine& stateMachine, StateContext& context);

    void handleEvent(const sf::Event& event) override;
    void update(sf::Time deltaTime) override;
    void render() override;

private:
    [[nodiscard]] bool isMouseOverPlayButton() const;
    void updateLayout();

    sf::CircleShape m_playButton;
    sf::Font m_font;
    sf::Text m_playText;
    bool m_shouldStartGame = false;
};
