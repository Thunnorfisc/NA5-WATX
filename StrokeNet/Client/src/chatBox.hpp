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

struct ChatBox
{
	sf::Font font;
	sf::FloatRect textClickBounds;
	sf::RectangleShape textBackground;
	sf::RectangleShape textTypingArea;

	sf::Text text;
	std::string currentInput;
	
	ChatBox(const std::string& fontPath);

	void draw(sf::RenderWindow& window);
	void sendMessageToServer(const std::string& id, const std::string& message);
	void receiveMessageFromServer(const std::string& name, const std::string& message);
};

