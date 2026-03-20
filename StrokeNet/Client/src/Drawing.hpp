#pragma once
#include <SFML/Graphics.hpp>

struct Point {
	float x, y;
};

struct Stroke {
	uint32_t id;
	sf::Color colour;
	float thickness;
	std::vector<Point> points;
};

struct RenderStroke {
	sf::VertexArray quads;
	std::vector<sf::CircleShape> joints;
};

struct Drawing {
	std::vector<RenderStroke> strokes;
};


RenderStroke BuildRenderStroke(const Stroke& stroke);
void AppendPoint(RenderStroke& rs, const Point& pt, const Stroke& stroke);
void DrawDrawing(sf::RenderWindow& window, const Drawing& drawing);