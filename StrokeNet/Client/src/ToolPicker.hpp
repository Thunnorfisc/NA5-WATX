/* Start Header
***********************************************************************/

/*! \file   ToolPicker.hpp
	\author William Wibisana Dumanauw
	\par    email: williamwibisana.d@digipen.edu
	\date   27th March, 2026
	\brief  Copyright (C) 2026 DigiPen Institute of Technology

	Reproduction or diclosure of this file or its contents without the prior
	written consent of DigiPen Institute of Technology is prohibited. */

	/* End Header
	***********************************************************************/
#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>

struct ToolEntry {
	std::string name;
	std::unique_ptr<sf::Texture> texture;  // heap-stable address for sprite reference
	std::unique_ptr<sf::Sprite>  sprite;   // constructed after texture loads
	sf::RectangleShape background;
	std::function<void()> onSelect;
};

struct ToolGroup {
	std::vector<ToolEntry> tools;
	int selectedIndex{ 0 };
};

struct ToolPicker {
	static constexpr float TOOL_SIZE = 40.f;
	static constexpr float PADDING = 6.f;
	static constexpr float GROUP_GAP = 10.f;

	std::vector<ToolGroup> groups;
	sf::RectangleShape selectionOutline;
	sf::Vector2f position;

	ToolPicker();

	int addGroup();

	int addTool(int groupIndex, const std::string& name,
		const std::string& iconPath,
		std::function<void()> callback);

	void select(int groupIndex, int toolIndex);

	[[nodiscard]] int getSelected(int groupIndex) const;

	void setPosition(sf::Vector2f pos);
	bool handleClick(sf::Vector2f mousePos);
	void draw(sf::RenderWindow& window);

	[[nodiscard]] float getTotalHeight() const;
};