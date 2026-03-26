/* Start Header
***********************************************************************/

/*! \file   Drawing.cpp
    \author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "Drawing.hpp"
#include <cmath>

RenderStroke BuildRenderStroke(const Stroke& stroke)
{
    RenderStroke rs;
    const auto& pts = stroke.points;
    float radius = stroke.thickness / 2.f;

    if (pts.empty()) return rs;

    for (const auto& p : pts) {
        sf::CircleShape circle(radius);
        circle.setOrigin(sf::Vector2f(radius, radius));
        circle.setPosition(sf::Vector2f(p.x, p.y));
        circle.setFillColor(stroke.colour);
        rs.joints.push_back(circle);
    }

    if (pts.size() < 2) return rs;

    rs.quads.setPrimitiveType(sf::PrimitiveType::TriangleStrip);

    for (size_t i = 0; i < pts.size() - 1; ++i) {
        sf::Vector2f p1(pts[i].x, pts[i].y);
        sf::Vector2f p2(pts[i + 1].x, pts[i + 1].y);

        sf::Vector2f dir = p2 - p1;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.0001f) continue;

        sf::Vector2f perp(-dir.y / len, dir.x / len);
        sf::Vector2f offset = perp * radius;

        sf::Vertex v;
        v.color = stroke.colour;

        if (i == 0) {
            v.position = p1 - offset; rs.quads.append(v);
            v.position = p1 + offset; rs.quads.append(v);
        }

        v.position = p2 - offset; rs.quads.append(v);
        v.position = p2 + offset; rs.quads.append(v);
    }

    return rs;
}

void AppendPoint(RenderStroke& rs, const Point& pt, const Stroke& stroke)
{
    float radius = stroke.thickness / 2.f;

    // Joint circle
    sf::CircleShape circle(radius);
    circle.setOrigin(sf::Vector2f(radius, radius));
    circle.setPosition(sf::Vector2f(pt.x, pt.y));
    circle.setFillColor(stroke.colour);
    rs.joints.push_back(circle);

    if (rs.joints.size() < 2) {
        return;
    }

    if (rs.joints.size() == 2) {
        sf::Vector2f p1 = rs.joints[0].getPosition();
        sf::Vector2f p2(pt.x, pt.y);

        sf::Vector2f dir = p2 - p1;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.0001f) return;

        sf::Vector2f perp(-dir.y / len, dir.x / len);
        sf::Vector2f offset = perp * radius;

        sf::Vertex v;
        v.color = stroke.colour;

        v.position = p1 - offset; rs.quads.append(v);
        v.position = p1 + offset; rs.quads.append(v);
        v.position = p2 - offset; rs.quads.append(v);
        v.position = p2 + offset; rs.quads.append(v);
        return;
    }

    sf::Vector2f p1 = rs.joints[rs.joints.size() - 2].getPosition();
    sf::Vector2f p2(pt.x, pt.y);

    sf::Vector2f dir = p2 - p1;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.0001f) return;

    sf::Vector2f perp(-dir.y / len, dir.x / len);
    sf::Vector2f offset = perp * radius;

    sf::Vertex v;
    v.color = stroke.colour;

    v.position = p2 - offset; rs.quads.append(v);
    v.position = p2 + offset; rs.quads.append(v);
}

void DrawDrawing(sf::RenderWindow& window, const Drawing& drawing)
{
    for (const auto& rs : drawing.strokes) {
        window.draw(rs.quads, sf::RenderStates(rs.blend));
        for (const auto& joint : rs.joints) {
            window.draw(joint, sf::RenderStates(rs.blend));
        }
    }
}
