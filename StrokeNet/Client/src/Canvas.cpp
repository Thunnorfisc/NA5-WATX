/* Start Header
***********************************************************************/

/*! \file   Canvas.cpp
	\author William Wibisana Dumanauw
	\par    email: williamwibisana.d@digipen.edu
	\date   20th March, 2026
	\brief  Copyright (C) 2026 DigiPen Institute of Technology

	Reproduction or diclosure of this file or its contents without the prior
	written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "Canvas.hpp"

Canvas::Canvas(sf::FloatRect rect) : bounds(rect)
{
	border.setPosition(sf::Vector2f(rect.position));
	border.setSize(sf::Vector2f(rect.size));
	border.setFillColor(sf::Color::White);
	border.setOutlineColor(sf::Color(80, 80, 80));
	border.setOutlineThickness(10.f);

	currentRender.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);
}

bool Canvas::contains(sf::Vector2f point) const
{
	return bounds.contains(point);
}

void Canvas::beginStroke(sf::Vector2f pos, sf::Color colour, float thickness) {
	isDrawing = true;
	currentStroke = {};
	currentStroke.id = nextId;
	currentStroke.colour = eraseMode ? sf::Color::Transparent : colour;
	currentStroke.thickness = thickness;
	currentRender = {};
	currentRender.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);
	currentRender.blend = eraseMode ? sf::BlendNone : sf::BlendAlpha;

	sf::Vector2f local = pos - bounds.position;
	Point pt{ local.x, local.y };
	currentStroke.points.push_back(pt);
	AppendPoint(currentRender, pt, currentStroke);
}

void Canvas::extendStroke(sf::Vector2f pos) {
	if (!isDrawing) return;

	sf::Vector2f local = pos - bounds.position;
	Point pt{ local.x, local.y };
	const auto& last = currentStroke.points.back();
	float dx = pt.x - last.x;
	float dy = pt.y - last.y;
	if (dx * dx + dy * dy < 4.f) return;

	currentStroke.points.push_back(pt);
	AppendPoint(currentRender, pt, currentStroke);
}

void Canvas::endStroke()
{
	if (!isDrawing) return;
	isDrawing = false;
	drawing.strokes.push_back(std::move(currentRender));
	currentRender = {};
	currentRender.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);
}

void Canvas::draw(sf::RenderWindow& window) {
	sf::View oldView = window.getView();

	window.draw(border);

	sf::FloatRect localRect({ 0.f, 0.f }, bounds.size);
	sf::View canvasView(localRect);

	sf::Vector2u winSize = window.getSize();
	sf::FloatRect viewport(
		{ bounds.position.x / winSize.x, bounds.position.y / winSize.y },
		{ bounds.size.x / winSize.x,     bounds.size.y / winSize.y }
	);
	canvasView.setViewport(viewport);
	window.setView(canvasView);

	DrawDrawing(window, drawing);

	if (isDrawing) {
		window.draw(currentRender.quads, sf::RenderStates(currentRender.blend));
		for (const auto& joint : currentRender.joints)
			window.draw(joint, sf::RenderStates(currentRender.blend));
	}

	window.setView(oldView);
}

void Canvas::clear()
{
	endStroke();
	drawing.strokes.clear();
	clearId++;
	eraseMode = false;
}
