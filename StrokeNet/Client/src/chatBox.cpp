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
const float TYPING_AREA_HEIGHT = 103.f;
const float TEXT_PADDING = 3.f;
const float MESSAGE_RECEIVED_SPACING = 5.f;
const float MAX_TEXT_WIDTH = CHATBOX_WIDTH - (2.f * TEXT_PADDING);

const uint8_t MAX_MESSAGE_LENGTH = 84;

ChatBox::ChatBox(const std::string& fontPath) : text(font), sampleText(font)
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
	sampleText.setString("Click and type here...");
	sampleText.setPosition({ CHATBOX_POSITION_X + TEXT_PADDING, CHATBOX_POSITION_Y + CHATBOX_HEIGHT - TYPING_AREA_HEIGHT });
}

void ChatBox::draw(sf::RenderWindow& window)
{
	window.draw(textBackground);
	window.draw(textTypingArea);
	window.draw(text);

	if (currentInput.empty() && !m_isTyping) window.draw(sampleText);

	if (m_isTyping) {
		sf::Text cursor(font, "|", text.getCharacterSize());
		cursor.setFillColor(sf::Color::Black);

		std::string wrappedStr = text.getString();
		size_t lastNewline = wrappedStr.rfind('\n');
		std::string lastLine = (lastNewline == std::string::npos) ? wrappedStr : wrappedStr.substr(lastNewline + 1);

		sf::Text lastLineText(font, lastLine, text.getCharacterSize());
		float lastLineWidth = lastLineText.getLocalBounds().size.x;

		sf::FloatRect textBounds = text.getLocalBounds();
		float textHeight = textBounds.size.y;

		float cursorX = text.getPosition().x + lastLineWidth;
		float cursorY;
		// sum hack lmao
		if (currentInput.empty()) cursorY = text.getPosition().y + textHeight;
		else cursorY = (text.getPosition().y + textHeight) - 10;
		cursor.setPosition({ cursorX, cursorY });
		window.draw(cursor);
	}

	for (auto& message : messagesReceivedFromServer) {
		window.draw(message);
	}


}

void ChatBox::sendMessageToServer(const std::string& name, const std::string& message)
{
#if _DEBUG
	std::cout << "Sending message: " << name << ": " << message << std::endl;
#endif

	// temp for now till it can receive from server
	receiveMessageFromServer("SnowPuppy", message);
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
				sendMessageToServer("SnowPuppy", currentInput);
				currentInput.clear();
				text.setString("");
			}
		}
		else if (ch >= 32 && ch < 127 && currentInput.size() < MAX_MESSAGE_LENGTH) {
			currentInput += static_cast<char>(ch);
			text.setString(wrapText(currentInput));
		}

#if _DEBUG
		std::cout << "Current input: " << currentInput << std::endl;
#endif
	}

}