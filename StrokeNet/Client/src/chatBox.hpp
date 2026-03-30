/* Start Header
***********************************************************************/

/*! \file   chatBox.hpp
	\author Alfred Lo Kai Xuan
	\par    email: alfredkaixuan.lo@digipen.edu
	\date   25th March, 2026
	\brief  Copyright (C) 2026 DigiPen Institute of Technology

	Reproduction or diclosure of this file or its contents without the prior
	written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#pragma once
#include <SFML/Graphics.hpp>
#include "Drawing.hpp"
#include <cstdint>
#include <vector>
#include <utility>

struct ChatBox
{
	sf::Font font;

	sf::FloatRect textClickBounds;

	sf::RectangleShape textBackground;
	sf::RectangleShape textTypingArea;
	sf::RectangleShape scoreboardBackground;
	sf::RectangleShape playerScoreboard;

	sf::Text text;
	sf::Text sampleText;
	sf::Text timerText;
	sf::Text playerName;
	sf::Text playerScore;
	sf::Text drawerTitle;

	std::string currentInput;
	std::string currentDrawerName;

	std::vector<sf::Text> messagesReceivedFromServer;
	std::vector<std::pair<std::string, uint16_t>> scoreboardData;
	std::uint8_t yourPlayerIndex{};

	std::uint32_t nextMessageId = 1;
	
	bool m_isTyping = false;

	ChatBox(const std::string& fontPath);

	void draw(sf::RenderWindow& window);
	void receiveMessageFromServer(const std::string& name, const std::string& message);
	void setTyping(bool typing) { m_isTyping = typing; }
	void handleEvent(const sf::Event& event);
	std::string wrapText(const std::string& input);
	void clearChatHistory();
	void displayTimer(std::int64_t seconds);
	void drawScoreboardOfPlayer(float posX, float posY, float scaleX, float scaleY, std::string name, uint16_t score, bool isDrawer, bool isYou);

};
