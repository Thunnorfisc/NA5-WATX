#pragma once
#include <SFML/Graphics.hpp>
#include "Drawing.hpp"

struct ChatBox
{
	sf::RectangleShape background;
	sf::Text text;
	std::string currentInput;
	ChatBox() = default;

	void sendMessageToServer(const std::string& message);

};

