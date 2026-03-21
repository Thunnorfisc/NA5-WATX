#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <array>

struct ColourPicker {
	static constexpr int COLS = 7;
	static constexpr float SWATCH_SIZE = 30.f;
	static constexpr float PADDING = 4.f;

	std::vector<sf::Color> palette;
	std::vector<sf::RectangleShape> swatches;
	sf::RectangleShape selectionOutline;
	sf::Vector2f position;
	unsigned int selectedIndex{};

	ColourPicker();

	void setPosition(sf::Vector2f pos);
	bool handleClick(sf::Vector2f mousePos);
	sf::Color getSelectedColour() const;
	void draw(sf::RenderWindow& window);
};