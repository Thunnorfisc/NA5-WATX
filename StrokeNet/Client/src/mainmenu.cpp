/* Start Header
***********************************************************************/

/*! \file   mainmenu.cpp
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
#include "mainmenu.hpp"
#include "client.hpp"
#include "state_machine.hpp"

#include <algorithm>

const float LEADERBOARD_BACKGROUND_WIDTH = 500.f;
const float LEADERBOARD_BACKGROUND_HEIGHT = 550.f;
const float LEADERBOARD_BACKGROUND_POSITION_X = 1000.f;
const float LEADERBOARD_BACKGROUND_POSITION_Y = 300.f;
const float PADDING = 5.f;
const float LEADERBOARD_ENTRY_HEIGHT = 40.f;

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

    void centerSprite(sf::Sprite& sprite, sf::Vector2f position)
    {
        const sf::FloatRect bounds = sprite.getLocalBounds();
        sprite.setOrigin({
            bounds.position.x + bounds.size.x * 0.5f,
            bounds.position.y + bounds.size.y * 0.5f
            });
        sprite.setPosition(position);
    }
}

MainMenuState::MainMenuState(StateMachine& stateMachine, StateContext& context) :
    State(stateMachine, context),
    m_font("resources/Marvel-Bold.ttf"),
    m_playText(m_font, "Play Game", 48),
    m_statusText(m_font, "", 20),
    m_GAMTITLE(m_font, "DigiStroke", 82),
	m_leaderboardTitleText(m_font, "Leaderboard", 40),
	m_drawnBy(m_font, "Artwork by Xavier Koh (RTIS), drawn in-game", 16)
{

	m_drawnBy.setFillColor(sf::Color(50, 50, 50));
	m_drawnBy.setPosition({ 50, 830 });

	m_playButton.setPosition({ 700, 450 });
	m_playButton.setSize({ 200, 80 });
    m_playButton.setFillColor(sf::Color(30, 30, 30));
    m_playButton.setOutlineThickness(6.0f);
    m_playButton.setOutlineColor(sf::Color(220, 70, 70));

    m_playText.setFillColor(sf::Color(230, 230, 230));
    m_playText.setOutlineThickness(2.0f);
    m_playText.setOutlineColor(sf::Color(70, 70, 70));

    m_statusText.setFillColor(sf::Color(230, 230, 230));
    m_statusText.setOutlineThickness(2.0f);
    m_statusText.setOutlineColor(sf::Color(220, 70, 70));
    
	m_leaderboardTitleText.setFillColor(sf::Color(160, 160, 180));
	centerText(m_leaderboardTitleText, { LEADERBOARD_BACKGROUND_POSITION_X + m_leaderboardTitleText.getLocalBounds().size.x/2, LEADERBOARD_BACKGROUND_POSITION_Y - m_leaderboardTitleText.getLocalBounds().size.y - PADDING});

    m_leaderboardBackground.setPosition({ LEADERBOARD_BACKGROUND_POSITION_X, LEADERBOARD_BACKGROUND_POSITION_Y });
    m_leaderboardBackground.setSize({ LEADERBOARD_BACKGROUND_WIDTH, LEADERBOARD_BACKGROUND_HEIGHT + PADDING});
    m_leaderboardBackground.setFillColor(sf::Color(80, 80, 80));

	centerText(m_GAMTITLE, { 800, 100 });
	m_GAMTITLE.setFillColor(sf::Color(220, 70, 70));
	m_GAMTITLE.setOutlineThickness(3.0f);
	m_GAMTITLE.setOutlineColor(sf::Color(230, 230, 230));


    updateLayout();
}

void MainMenuState::handleEvent(const sf::Event& event)
{
    updateLayout();

    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>())
    {
        if (mousePressed->button == sf::Mouse::Button::Left && isMouseOverPlayButton())
        {
            auto pgs = Client::playGame();
            
            if (pgs.first == PlayGameStatus::SUCCESS)
            {
                m_shouldStartGame = true;
                if (pgs.second.has_value()) m_currRoundAndTotalRound = *pgs.second;
            }
            else if (pgs.first == PlayGameStatus::TOO_MANY_PLAYERS) m_statusText.setString("Server lobby full!");
            else if (pgs.first == PlayGameStatus::SERVER_NO_RESPONSE) m_statusText.setString("Server no response! Try again later");
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

    auto leaderboardOpt = Client::getLeaderboard();
    if (leaderboardOpt) {
        leaderboardData = leaderboardOpt->_leaderboardEntries;
        m_localPlayerIndex = leaderboardOpt->_playerIndex;
		m_localPlayerScore = leaderboardOpt->_playerScore;
    }

    if (isMouseOverPlayButton()) {
        m_playButton.setFillColor(sf::Color(80, 80, 80));
        m_playButton.setOutlineColor(sf::Color(255, 100, 100));
    }
    else {
        m_playButton.setFillColor(sf::Color(30, 30, 30));
		m_playButton.setOutlineColor(sf::Color(220, 70, 70));
    }

    if (m_shouldStartGame)
    {
        m_shouldStartGame = false;
        context().roundInfo = m_currRoundAndTotalRound;
        requestStateChange(StateId::CoreGame);
    }
}

void MainMenuState::render()
{
    auto& window = context().window;

	sf::Texture backgroundTexture("resources/sprites/TailzedArt.png");
	sf::Sprite backgroundSprite(backgroundTexture);
	centerSprite(backgroundSprite, { 400, 450 });
	backgroundSprite.setScale({ 1.5f, 1.5f });

	window.draw(backgroundSprite);
    window.draw(m_drawnBy);
    window.draw(m_leaderboardBackground);
    window.draw(m_GAMTITLE);
    window.draw(m_playButton);
    window.draw(m_playText);
    window.draw(m_statusText);
    window.draw(m_leaderboardTitleText);

	uint8_t i = 1;
	float currentY = LEADERBOARD_BACKGROUND_POSITION_Y + PADDING;
    static float toAddY = (LEADERBOARD_BACKGROUND_HEIGHT / 5) - PADDING;
    for (const auto& [name, score] : leaderboardData) {

		m_leaderboardEntriesBackground.setPosition({ LEADERBOARD_BACKGROUND_POSITION_X + PADDING, currentY});
        m_leaderboardEntriesBackground.setSize({ LEADERBOARD_BACKGROUND_WIDTH - PADDING * 2, toAddY });
        if (i - 1 == m_localPlayerIndex) {
            m_leaderboardEntriesBackground.setFillColor(sf::Color(40, 100, 100));
        }
        else {
            m_leaderboardEntriesBackground.setFillColor(sf::Color(40, 40, 40));
        }

        sf::Text entryRank(m_font, std::to_string(i), 48);
        if (i == 1) entryRank.setFillColor(sf::Color(255, 215, 0));
        else if (i == 2) entryRank.setFillColor(sf::Color(192, 192, 192));
        else if (i == 3) entryRank.setFillColor(sf::Color(205, 127, 50));
		else entryRank.setFillColor(sf::Color(230, 230, 230));
		entryRank.setOutlineThickness(1.0f);
		entryRank.setOutlineColor(sf::Color(70, 70, 70));
		centerText(entryRank, { LEADERBOARD_BACKGROUND_POSITION_X + PADDING * 6, currentY + toAddY / 2 });

		sf::Text entryName(m_font, name, 45);
		entryName.setFillColor(sf::Color(230, 230, 230));
		entryName.setOutlineThickness(1.0f);
		entryName.setOutlineColor(sf::Color(70, 70, 70));
		centerText(entryName, { LEADERBOARD_BACKGROUND_POSITION_X + LEADERBOARD_BACKGROUND_WIDTH / 2 - PADDING * 6, currentY + toAddY / 2 });

		sf::Text entryScore(m_font, std::to_string(score), 45);
		entryScore.setFillColor(sf::Color(230, 230, 230));
		entryScore.setOutlineThickness(1.0f);
		entryScore.setOutlineColor(sf::Color(70, 70, 70));
		centerText(entryScore, { LEADERBOARD_BACKGROUND_POSITION_X + LEADERBOARD_BACKGROUND_WIDTH - PADDING * 12, currentY + toAddY / 2 });

        window.draw(m_leaderboardEntriesBackground);
		window.draw(entryRank);
		window.draw(entryName);
		window.draw(entryScore);

        currentY += toAddY + PADDING;
        i += 1;
	}


}

bool MainMenuState::isMouseOverPlayButton() const
{
    const sf::Vector2i pixelPosition = sf::Mouse::getPosition(context().window);
    const sf::Vector2f worldPosition = context().window.mapPixelToCoords(pixelPosition);
    return m_playButton.getGlobalBounds().contains(worldPosition);
}

void MainMenuState::updateLayout()
{
    //const sf::Vector2u windowSize = context().window.getSize();
    //const float scale = std::min(windowSize.x / 1600.0f, windowSize.y / 900.0f);
    //const sf::Vector2f center(windowSize.x * 0.5f, windowSize.y * 0.5f);

    //m_playButton.setScale({ scale, scale });
    //m_playButton.setPosition(center);

    centerText(m_playText, m_playButton.getGlobalBounds().getCenter());
    centerText(m_statusText, m_playButton.getGlobalBounds().getCenter());

    auto stpos = m_statusText.getPosition();
    m_statusText.setPosition(sf::Vector2f(stpos.x, stpos.y - (m_playButton.getGlobalBounds().size.y * 1.25f)));
}
