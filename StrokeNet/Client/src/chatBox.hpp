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

