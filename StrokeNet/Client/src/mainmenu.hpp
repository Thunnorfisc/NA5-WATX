/* Start Header
***********************************************************************/

/*! \file   mainmenu.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \co-author Alfred Lo Kai Xuan
    \par    email: alfredkaixuan.lo@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
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
    std::vector<std::pair<std::string, uint16_t>> leaderboardData;

private:
    [[nodiscard]] bool isMouseOverPlayButton() const;
    void updateLayout();

    sf::CircleShape m_playButton;
    sf::CircleShape m_practiceButton;
    sf::Font m_font;
    sf::Text m_playText;
    bool m_shouldStartGame = false;
    sf::Text m_statusText;
};
