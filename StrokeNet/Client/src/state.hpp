/* Start Header
***********************************************************************/

/*! \file   state.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#pragma once

#include <SFML/Graphics.hpp>

#include <optional>

enum class StateId
{
    Login,
    MainMenu,
    CoreGame
};

struct StateContext
{
    sf::RenderWindow& window;
    std::optional<sf::Event> event;
    std::pair<std::uint8_t, std::uint8_t> roundInfo{ 255,255 };
};

class StateMachine;

class State
{
public:
    State(StateMachine& stateMachine, StateContext& context);
    virtual ~State() = default;

    virtual void handleEvent(const sf::Event& event);
    virtual void update(sf::Time deltaTime) = 0;
    virtual void render() = 0;

protected:
    void requestStateChange(StateId stateId);
    [[nodiscard]] StateContext& context();
    [[nodiscard]] const StateContext& context() const;

private:
    StateMachine& m_stateMachine;
    StateContext& m_context;
};
