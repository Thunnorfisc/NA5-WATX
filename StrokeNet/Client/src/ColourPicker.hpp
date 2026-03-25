/* Start Header
***********************************************************************/

/*! \file   ColourPicker.hpp
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