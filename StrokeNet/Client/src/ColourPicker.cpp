#include "ColourPicker.hpp"

ColourPicker::ColourPicker()
{
    palette = {
        sf::Color::Black, sf::Color::White,
        sf::Color::Red, sf::Color::Blue,
        sf::Color::Yellow, sf::Color::Green,
        sf::Color::Cyan, sf::Color::Magenta
    };

    for (size_t i{}; i < palette.size(); ++i) {
        sf::RectangleShape swatch({SWATCH_SIZE, SWATCH_SIZE});
        swatch.setFillColor(palette[i]);
        swatch.setOutlineColor(sf::Color(60, 60, 60));
        swatch.setOutlineThickness(1.f);
        swatches.push_back(swatch);
    }

    selectionOutline.setSize({ SWATCH_SIZE + 4.f, SWATCH_SIZE + 4.f });
    selectionOutline.setFillColor(sf::Color::Transparent);
    selectionOutline.setOutlineColor(sf::Color(220, 70, 70));
    selectionOutline.setOutlineThickness(5.f);
}

void ColourPicker::setPosition(sf::Vector2f pos)
{
    position = pos;
    for (size_t i{}; i < swatches.size(); ++i) {
        int col = i % COLS;
        int row = i / COLS;
        swatches[i].setPosition({
            pos.x + col * (SWATCH_SIZE + PADDING),
            pos.y + row * (SWATCH_SIZE + PADDING)
        });
    }
}

bool ColourPicker::handleClick(sf::Vector2f mousePos)
{
    for (size_t i = 0; i < swatches.size(); ++i) {
        if (swatches[i].getGlobalBounds().contains(mousePos)) {
            selectedIndex = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

sf::Color ColourPicker::getSelectedColour() const
{
    return palette[selectedIndex];
}

void ColourPicker::draw(sf::RenderWindow& window)
{
    for (const auto& swatch : swatches) {
        window.draw(swatch);

        selectionOutline.setPosition(
            swatches[selectedIndex].getPosition() - sf::Vector2f(2.f, 2.f)
        );
        window.draw(selectionOutline);
    }
}
