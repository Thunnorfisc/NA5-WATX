/* Start Header
***********************************************************************/

/*! \file   Drawing.hpp
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

struct Point {
	float x, y;
};

struct Stroke {
	uint32_t id{};
	sf::Color colour;
	float thickness{ 1.0f };
	std::vector<Point> points;
};

struct RenderStroke {
	sf::VertexArray quads;
	std::vector<sf::CircleShape> joints;
	sf::BlendMode blend = sf::BlendAlpha;
};

struct Drawing {
	std::vector<RenderStroke> strokes;
};


RenderStroke BuildRenderStroke(const Stroke& stroke);
void AppendPoint(RenderStroke& rs, const Point& pt, const Stroke& stroke);
void DrawDrawing(sf::RenderWindow& window, const Drawing& drawing);