#pragma once
#include <SFML/Graphics.hpp>
#include "Drawing.hpp"

struct Canvas {
	sf::FloatRect bounds;
	sf::RectangleShape border;
	Drawing drawing;

	Stroke currentStroke;
	RenderStroke currentRender;
	bool isDrawing = false;
	uint32_t nextId = 1;

	Canvas() = default;
	Canvas(sf::FloatRect rect);

	bool contains(sf::Vector2f point) const;
	void beginStroke(sf::Vector2f pos, sf::Color colour, float thickness);
	void extendStroke(sf::Vector2f pos);
	void endStroke();
	void draw(sf::RenderWindow& window);
	void clear();
};