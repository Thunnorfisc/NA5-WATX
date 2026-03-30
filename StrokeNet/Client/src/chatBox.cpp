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
#include "shared_protocol.hpp"
#include "chatBox.hpp"
#include "client.hpp"
#include <iostream>

const float CHATBOX_WIDTH = 310.f;
const float CHATBOX_HEIGHT = 700.f;
const float CHATBOX_POSITION_X = 1270.f;
const float CHATBOX_POSITION_Y = 120.f;
const float TYPING_AREA_HEIGHT = 103.f;
const float TEXT_PADDING = 3.f;
const float MESSAGE_RECEIVED_SPACING = 5.f;
const float MAX_TEXT_WIDTH = CHATBOX_WIDTH - (2.f * TEXT_PADDING);
const float SCOREBOARD_BACKGROUND_POSITION_X = 20.f;
const float SCOREBOARD_BACKGROUND_POSITION_Y = CHATBOX_POSITION_Y - 50.f;

const uint8_t MAX_MESSAGE_LENGTH = static_cast<std::uint8_t>(MAX_CHARS_PER_CHAT_MSG);

ChatBox::ChatBox(const std::string& fontPath) : text(font), sampleText(font), timerText(font), playerName(font), playerScore(font)
{
	if (!font.openFromFile(fontPath)) throw std::runtime_error("Failed to load font from: " + fontPath);

	textBackground.setPosition({ CHATBOX_POSITION_X, CHATBOX_POSITION_Y });
	textBackground.setSize(sf::Vector2f(CHATBOX_WIDTH, CHATBOX_HEIGHT));
	textBackground.setFillColor(sf::Color(120, 120, 120));
	textBackground.setOutlineThickness(2.f);
	textBackground.setOutlineColor(sf::Color(80, 80, 80));

	textTypingArea.setPosition({ CHATBOX_POSITION_X, CHATBOX_POSITION_Y + CHATBOX_HEIGHT - TYPING_AREA_HEIGHT });
	textTypingArea.setSize(sf::Vector2f(CHATBOX_WIDTH, TYPING_AREA_HEIGHT));
	textTypingArea.setFillColor(sf::Color(200, 200, 240));
	textTypingArea.setOutlineThickness(1.f);
	textTypingArea.setOutlineColor(sf::Color(100, 100, 100));

	text.setCharacterSize(21);
	text.setFillColor(sf::Color::Black);
	text.setPosition({ CHATBOX_POSITION_X + TEXT_PADDING, CHATBOX_POSITION_Y + CHATBOX_HEIGHT - TYPING_AREA_HEIGHT });

	sampleText.setCharacterSize(21);
	sampleText.setFillColor(sf::Color::Black);
	sampleText.setString("Press 'Enter' or Click here to type...");
	sampleText.setPosition({ CHATBOX_POSITION_X + TEXT_PADDING, CHATBOX_POSITION_Y + CHATBOX_HEIGHT - TYPING_AREA_HEIGHT });

	timerText.setString("");
	timerText.setCharacterSize(64);
	timerText.setFillColor(sf::Color::Red);
	timerText.setStyle(sf::Text::Bold);
	timerText.setOutlineThickness(2.f);
	timerText.setOutlineColor(sf::Color::White);
	timerText.setPosition({ 400.f, 100.f });

	scoreboardBackground.setPosition({ SCOREBOARD_BACKGROUND_POSITION_X, SCOREBOARD_BACKGROUND_POSITION_Y });
	scoreboardBackground.setSize(sf::Vector2f(CHATBOX_WIDTH, CHATBOX_HEIGHT));
	scoreboardBackground.setFillColor(sf::Color(120, 120, 120));
	scoreboardBackground.setOutlineThickness(1.f);
	scoreboardBackground.setOutlineColor(sf::Color(80, 80, 80));
}

void ChatBox::draw(sf::RenderWindow& window)
{
	window.draw(textBackground);
	window.draw(textTypingArea);
	window.draw(text);
	window.draw(timerText);
	window.draw(scoreboardBackground);

	if (currentInput.empty() && !m_isTyping) window.draw(sampleText);

	if (m_isTyping) {
		sf::Text cursor(font, "|", text.getCharacterSize());
		cursor.setFillColor(sf::Color::Black);

		std::string wrappedStr = text.getString();
		size_t lastNewline = wrappedStr.rfind('\n');
		std::string lastLine = (lastNewline == std::string::npos) ? wrappedStr : wrappedStr.substr(lastNewline + 1);

		sf::Text lastLineText(font, lastLine, text.getCharacterSize());
		float lastLineWidth = lastLineText.getLocalBounds().size.x;

		float lineHeight = font.getLineSpacing(text.getCharacterSize());
		int lineCount = static_cast<int>(std::count(wrappedStr.begin(), wrappedStr.end(), '\n'));

		float cursorX = text.getPosition().x + lastLineWidth;
		float cursorY = text.getPosition().y + lineCount * lineHeight;
		cursor.setPosition({ cursorX, cursorY });
		window.draw(cursor);
	}

	for (auto& message : messagesReceivedFromServer) {
		window.draw(message);
	}

	if (auto sb = Client::getLatestScoreboard()) {
		scoreboardData = sb->_users;
		currentDrawerName = sb->_currentDrawer;
		std::cout << "Current drawer: " << currentDrawerName << std::endl;
		std::cout << "Scoreboard data:" << std::endl;
#ifdef _DEBUG
		for (const auto& [name, score] : scoreboardData) {
			std::cout << " - " << name << ": " << score << std::endl;
		}
#endif
	}

	static float currentPosY = SCOREBOARD_BACKGROUND_POSITION_Y + 10.f;
	static float toAddY = (CHATBOX_HEIGHT / 6.f) - 15.f;
	for (const auto& [name, score] : scoreboardData) {
		
		drawScoreboardOfPlayer(SCOREBOARD_BACKGROUND_POSITION_X + 10.f, currentPosY, CHATBOX_WIDTH - 20.f, toAddY, name, score);
		window.draw(playerScoreboard);
		window.draw(playerName);
		window.draw(playerScore);
		currentPosY += toAddY + MESSAGE_RECEIVED_SPACING;
	}
	currentPosY = SCOREBOARD_BACKGROUND_POSITION_Y + 10.f;

}

void ChatBox::receiveMessageFromServer(const std::string& name, const std::string& message)
{
	sf::Text newMessage(font, wrapText("[" + name + "]: " + message), text.getCharacterSize());
	newMessage.setFillColor(sf::Color::Black);

	float messageHeight = newMessage.getLocalBounds().size.y;
	for (auto& msg : messagesReceivedFromServer) {
		msg.setPosition({ msg.getPosition().x, msg.getPosition().y - messageHeight - MESSAGE_RECEIVED_SPACING });
	}

	float messageY = CHATBOX_POSITION_Y + CHATBOX_HEIGHT - TYPING_AREA_HEIGHT - MESSAGE_RECEIVED_SPACING - messageHeight;
	newMessage.setPosition({ CHATBOX_POSITION_X + MESSAGE_RECEIVED_SPACING, messageY });
	messagesReceivedFromServer.push_back(newMessage);
}

std::string ChatBox::wrapText(const std::string& input)
{
	std::string wrappedText;
	std::string currentLine;

	for (int i = 0; i < input.length(); ++i) {
		currentLine += input[i];

		sf::Text tempText(font, currentLine, text.getCharacterSize());
		if (tempText.getLocalBounds().size.x > MAX_TEXT_WIDTH) {
			currentLine.pop_back();
			wrappedText += currentLine + "\n";
			currentLine = "";
			currentLine += input[i];
		}
	}

	wrappedText += currentLine;
	return wrappedText;
}

void ChatBox::handleEvent(const sf::Event& event)
{
	if (!m_isTyping) return;

	// ty william
	if (const auto* textEntered = event.getIf<sf::Event::TextEntered>()) {
		std::uint32_t ch = textEntered->unicode;

		if (ch == ' ' && text.getString() == "") return;

		if (ch == '\b') { // bckspace
			if (!currentInput.empty()) {
				currentInput.pop_back();
				text.setString(wrapText(currentInput));
			}
		}
		else if (ch == '\r' || ch == '\n') { // enter
			if (!currentInput.empty()) {
				if (!currentInput.empty()) Client::sendChatMessage(nextMessageId++, currentInput);
				currentInput.clear();
				text.setString("");
			}
		}
		else if ((ch >= 32 && ch < 127) && currentInput.size() < MAX_MESSAGE_LENGTH) {
			currentInput += static_cast<char>(ch);
			text.setString(wrapText(currentInput));
		}

#ifdef _DEBUG
		std::cout << "Current input: " << currentInput << std::endl;
#endif
	}

}

void ChatBox::clearChatHistory() {
	messagesReceivedFromServer.clear();
}

void ChatBox::displayTimer(std::int64_t seconds) {
	timerText.setString(std::to_string(seconds) + "s");
}

void ChatBox::drawScoreboardOfPlayer(float posX, float posY, float scaleX, float scaleY, std::string name, uint16_t score) {
	playerScoreboard.setPosition({ posX, posY });
	playerScoreboard.setSize(sf::Vector2f(scaleX, scaleY));
	playerScoreboard.setFillColor(sf::Color(40, 40, 40));
	playerScoreboard.setOutlineThickness(1.f);
	playerScoreboard.setOutlineColor(sf::Color::Black);

	playerName.setString(name);
	playerName.setCharacterSize(32);
	playerName.setPosition({ posX + TEXT_PADDING, posY + TEXT_PADDING });
	if (name == currentDrawerName) {
		playerName.setFillColor(sf::Color::Green);
		playerName.setStyle(sf::Text::Bold);
		playerName.setOutlineThickness(4.f);
		playerName.setOutlineColor(sf::Color::Red);
	}
	else {
		playerName.setFillColor(sf::Color::White);
		playerName.setStyle(sf::Text::Regular);
		playerName.setOutlineThickness(1.5f);
		playerName.setOutlineColor(sf::Color(96, 96, 96));
	}

	playerScore.setString(std::to_string(score));
	playerScore.setStyle(sf::Text::Bold);
	playerScore.setCharacterSize(30);
	playerScore.setPosition({ posX + TEXT_PADDING, posY + TEXT_PADDING * 3 + playerName.getCharacterSize()});
	playerScore.setFillColor(sf::Color::Yellow);
	playerScore.setOutlineThickness(1.f);
	playerScore.setOutlineColor(sf::Color::Blue);
}