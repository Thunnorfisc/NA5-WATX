/* Start Header
***********************************************************************/

/*! \file   chatBox.cpp
	\author Alfred Lo Kai Xuan
	\par    email: alfredkaixuan.lo@digipen.edu
	\date   25th March, 2026
	\brief  Copyright (C) 2026 DigiPen Institute of Technology

	Reproduction or diclosure of this file or its contents without the prior
	written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "chatBox.hpp"
#include <iostream>

const float CHATBOX_WIDTH = 310.f;
const float CHATBOX_HEIGHT = 700.f;
const float CHATBOX_POSITION_X = 1270.f;
const float CHATBOX_POSITION_Y = 120.f;
const float TYPING_AREA_HEIGHT = 50.f;

ChatBox::ChatBox(const std::string& fontPath) : text(font)
{
	if (!font.openFromFile(fontPath))
	{
		throw std::runtime_error("Failed to load font from: " + fontPath);
	}

	textBackground.setPosition({ CHATBOX_POSITION_X, CHATBOX_POSITION_Y });
	textBackground.setSize(sf::Vector2f(CHATBOX_WIDTH, CHATBOX_HEIGHT));
	textBackground.setFillColor(sf::Color(120,120,120));
	textBackground.setOutlineThickness(2.f);
	textBackground.setOutlineColor(sf::Color(80, 80, 80));

	textTypingArea.setPosition({ CHATBOX_POSITION_X, CHATBOX_POSITION_Y + CHATBOX_HEIGHT - TYPING_AREA_HEIGHT});
	textTypingArea.setSize(sf::Vector2f(CHATBOX_WIDTH, TYPING_AREA_HEIGHT));
	textTypingArea.setFillColor(sf::Color(200, 200, 240));
	textTypingArea.setOutlineThickness(1.f);
	textTypingArea.setOutlineColor(sf::Color(100, 100, 100));

	text.setCharacterSize(16);
	text.setFillColor(sf::Color::Black);
	text.setPosition({ 10, 10 });
}

void ChatBox::draw(sf::RenderWindow& window)
{
	window.draw(textBackground);
	window.draw(textTypingArea);
	window.draw(text);
}

void ChatBox::sendMessage(const std::string& name, const std::string& message)
{
	std::cout << "Sending message: " << name << ": " << message << std::endl;
}

void ChatBox::receiveMessageFromServer(const std::string& name, const std::string& message)
{
	
	currentInput = name + ": " + message;
	text.setString(currentInput);
}

bool ChatBox::handleChatBox()
{
	if (m_isTyping) {
		if (sf::Keyboard::isKeyPressed(static_cast<sf::Keyboard::Key>(58))) {
			sendMessage("SnowPuppy", currentInput);
			currentInput.clear();
			return true;
		}
		else if (sf::Keyboard::isKeyPressed(static_cast<sf::Keyboard::Key>(59))) {
			if (!currentInput.empty()) {
				currentInput.pop_back();
				text.setString(currentInput);
				std::cout << "Current input: " << currentInput << std::endl;
			}
		}
		for (int i = 0; i < 26; ++i) {
			if (sf::Keyboard::isKeyPressed(static_cast<sf::Keyboard::Key>(i))) {
				currentInput += static_cast<char>('a' + i);
				text.setString(currentInput);
				std::cout << "Current input: " << currentInput << std::endl;
			}
		}
		return true;
	}
	return false;
}