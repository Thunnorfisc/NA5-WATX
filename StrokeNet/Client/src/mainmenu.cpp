/* Start Header
***********************************************************************/

/*! \file   mainmenu.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "mainmenu.hpp"
#include "client.hpp"
#include "state_machine.hpp"

#include <algorithm>

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

MainMenuState::MainMenuState(StateMachine& stateMachine, StateContext& context) :
    State(stateMachine, context),
    m_font("resources/Marvel-Bold.ttf"),
    m_playText(m_font, "Play Game", 52)
{
    m_playButton.setRadius(180.0f);
    m_playButton.setOrigin({ 180.0f, 180.0f });
    m_playButton.setFillColor(sf::Color(30, 30, 30));
    m_playButton.setOutlineThickness(6.0f);
    m_playButton.setOutlineColor(sf::Color(220, 70, 70));

    m_playText.setFillColor(sf::Color(230, 230, 230));
    m_playText.setOutlineThickness(2.0f);
    m_playText.setOutlineColor(sf::Color(220, 70, 70));

    updateLayout();
}

void MainMenuState::handleEvent(const sf::Event& event)
{
    updateLayout();

    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>())
    {
        if (mousePressed->button == sf::Mouse::Button::Left && isMouseOverPlayButton() &&
            Client::playGame())
        {
            m_shouldStartGame = true;
        }
    }
 //   else if (const auto* keyboardPressed = event.getIf<sf::Event::KeyPressed>())
 //   {
 //       if (keyboardPressed->code == sf::Keyboard::Key::Enter || keyboardPressed->code == sf::Keyboard::Key::Space)
 //       {
 //           m_shouldStartGame = true;
 //       }
	//}
}

void MainMenuState::update(sf::Time)
{
    updateLayout();

    if (m_shouldStartGame)
    {
        m_shouldStartGame = false;
        requestStateChange(StateId::CoreGame);
    }
}

void MainMenuState::render()
{
    auto& window = context().window;
    window.draw(m_playButton);
    window.draw(m_playText);
}

bool MainMenuState::isMouseOverPlayButton() const
{
    const sf::Vector2i pixelPosition = sf::Mouse::getPosition(context().window);
    const sf::Vector2f worldPosition = context().window.mapPixelToCoords(pixelPosition);
    return m_playButton.getGlobalBounds().contains(worldPosition);
}

void MainMenuState::updateLayout()
{
    const sf::Vector2u windowSize = context().window.getSize();
    const float scale = std::min(windowSize.x / 1600.0f, windowSize.y / 900.0f);
    const sf::Vector2f center(windowSize.x * 0.5f, windowSize.y * 0.5f);

    m_playButton.setScale({ scale, scale });
    m_playButton.setPosition(center);

    centerText(m_playText, m_playButton.getGlobalBounds().getCenter());
}
