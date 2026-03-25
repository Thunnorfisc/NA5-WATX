/* Start Header
***********************************************************************/

/*! \file   Canvas.hpp
	\author William Wibisana Dumanauw
	\par    email: williamwibisana.d@digipen.edu
	\date   20th March, 2026
	\brief  Copyright (C) 2026 DigiPen Institute of Technology

	Reproduction or diclosure of this file or its contents without the prior
	written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
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
	uint32_t clearId = 0;

	Canvas() = default;
	Canvas(sf::FloatRect rect);

	bool contains(sf::Vector2f point) const;
	void beginStroke(sf::Vector2f pos, sf::Color colour, float thickness);
	void extendStroke(sf::Vector2f pos);
	void endStroke();
	void draw(sf::RenderWindow& window);
	void clear();
};