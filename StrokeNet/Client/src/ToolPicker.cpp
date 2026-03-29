/* Start Header
***********************************************************************/

/*! \file   ToolPicker.cpp
	\author William Wibisana Dumanauw
	\par    email: williamwibisana.d@digipen.edu
	\date   27th March, 2026
	\brief  Copyright (C) 2026 DigiPen Institute of Technology

	Reproduction or diclosure of this file or its contents without the prior
	written consent of DigiPen Institute of Technology is prohibited. */

	/* End Header
	***********************************************************************/
#include "ToolPicker.hpp"
#include <iostream>
#include <algorithm>

ToolPicker::ToolPicker()
{
	selectionOutline.setSize({ TOOL_SIZE + 4.f, TOOL_SIZE + 4.f });
	selectionOutline.setFillColor(sf::Color::Transparent);
	selectionOutline.setOutlineColor(sf::Color(220, 70, 70));
	selectionOutline.setOutlineThickness(3.f);
}

int ToolPicker::addGroup()
{
	groups.emplace_back();
	return static_cast<int>(groups.size()) - 1;
}

int ToolPicker::addTool(int groupIndex, const std::string& name,
	const std::string& iconPath,
	std::function<void()> callback)
{
	if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size())) {
		std::cerr << "[ToolPicker] Invalid group index: " << groupIndex << "\n";
		return -1;
	}

	auto& group = groups[groupIndex];
	ToolEntry entry;
	entry.name = name;
	entry.onSelect = std::move(callback);

	// Heap-allocate texture so its address stays stable across vector resizes
	entry.texture = std::make_unique<sf::Texture>(iconPath);
	entry.texture->setSmooth(true);

	// Construct sprite from the stable texture pointer
	entry.sprite = std::make_unique<sf::Sprite>(*entry.texture);
	auto texSize = entry.texture->getSize();
	if (texSize.x > 0 && texSize.y > 0) {
		float scale = (TOOL_SIZE - 8.f) / static_cast<float>(std::max(texSize.x, texSize.y));
		entry.sprite->setScale({ scale, scale });
	}

	// Background box
	entry.background.setSize({ TOOL_SIZE, TOOL_SIZE });
	entry.background.setFillColor(sf::Color(50, 50, 50));
	entry.background.setOutlineColor(sf::Color(80, 80, 80));
	entry.background.setOutlineThickness(1.f);

	group.tools.push_back(std::move(entry));
	int toolIdx = static_cast<int>(group.tools.size()) - 1;

	// Recalculate positions
	setPosition(position);

	return toolIdx;
}

void ToolPicker::select(int groupIndex, int toolIndex)
{
	if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size())) return;
	auto& group = groups[groupIndex];
	if (toolIndex < 0 || toolIndex >= static_cast<int>(group.tools.size())) return;

	group.selectedIndex = toolIndex;
	if (group.tools[toolIndex].onSelect) {
		group.tools[toolIndex].onSelect();
	}
}

int ToolPicker::getSelected(int groupIndex) const
{
	if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size())) return -1;
	return groups[groupIndex].selectedIndex;
}

void ToolPicker::setPosition(sf::Vector2f pos)
{
	position = pos;

	float yOffset = 0.f;
	for (auto& group : groups) {
		for (size_t i = 0; i < group.tools.size(); ++i) {
			float x = pos.x + i * (TOOL_SIZE + PADDING);
			float y = pos.y + yOffset;

			group.tools[i].background.setPosition({ x, y });

			// Center the sprite icon within the background
			if (group.tools[i].sprite && group.tools[i].texture) {
				auto texSize = group.tools[i].texture->getSize();
				float scale = group.tools[i].sprite->getScale().x;
				float spriteW = texSize.x * scale;
				float spriteH = texSize.y * scale;
				group.tools[i].sprite->setPosition({
					x + (TOOL_SIZE - spriteW) / 2.f,
					y + (TOOL_SIZE - spriteH) / 2.f
					});
			}
		}
		if (!group.tools.empty()) {
			yOffset += TOOL_SIZE + GROUP_GAP;
		}
	}
}

bool ToolPicker::handleClick(sf::Vector2f mousePos)
{
	for (int g = 0; g < static_cast<int>(groups.size()); ++g) {
		auto& group = groups[g];
		for (int i = 0; i < static_cast<int>(group.tools.size()); ++i) {
			if (group.tools[i].background.getGlobalBounds().contains(mousePos)) {
				select(g, i);
				return true;
			}
		}
	}
	return false;
}

void ToolPicker::draw(sf::RenderWindow& window)
{
	for (auto& group : groups) {
		for (auto& tool : group.tools) {
			window.draw(tool.background);
			if (tool.sprite) {
				window.draw(*tool.sprite);
			}
		}

		if (group.selectedIndex >= 0 &&
			group.selectedIndex < static_cast<int>(group.tools.size()))
		{
			auto& sel = group.tools[group.selectedIndex];
			selectionOutline.setPosition(
				sel.background.getPosition() - sf::Vector2f(2.f, 2.f)
			);
			window.draw(selectionOutline);
		}
	}
}

float ToolPicker::getTotalHeight() const
{
	if (groups.empty()) return 0.f;
	float h = 0.f;
	int nonEmpty = 0;
	for (const auto& g : groups) {
		if (!g.tools.empty()) {
			h += TOOL_SIZE;
			++nonEmpty;
		}
	}
	if (nonEmpty > 1) h += (nonEmpty - 1) * GROUP_GAP;
	return h;
}