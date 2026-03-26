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
struct ChatBox
{
	sf::Font font;
	sf::FloatRect textClickBounds;
	sf::RectangleShape textBackground;
	sf::RectangleShape textTypingArea;

	sf::Text text;
	sf::Text sampleText;
	sf::Text timerText;
	std::string currentInput;
	std::vector<sf::Text> messagesReceivedFromServer;

	std::uint32_t nextMessageId = 1;
	
	bool m_isTyping = false;

	ChatBox(const std::string& fontPath);

	void handleChatBox();
	void draw(sf::RenderWindow& window);
	void sendMessageToServer(const std::string& name, const std::string& message);
	void receiveMessageFromServer(const std::string& name, const std::string& message);
	void setTyping(bool typing) { m_isTyping = typing; }
	void handleEvent(const sf::Event& event);
	std::string wrapText(const std::string& input);
	void clearChatHistory();
	void displayTimer(std::int64_t seconds);
};
