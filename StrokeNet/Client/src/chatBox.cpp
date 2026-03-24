#include "chatBox.hpp"

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

void ChatBox::sendMessageToServer(const std::string& id, const std::string& message)
{
	
}

void ChatBox::receiveMessageFromServer(const std::string& name, const std::string& message)
{
	
	currentInput = name + ": " + message;
	text.setString(currentInput);
}