/* Start Header
***********************************************************************/

/*! \file   state_machine.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#pragma once

#include "state.hpp"

#include <memory>
#include <optional>

class StateMachine
{
public:
    explicit StateMachine(StateContext& context);

    void changeState(StateId stateId);
    void handleEvent(const sf::Event& event);
    void update(sf::Time deltaTime);
    void render();

private:
    void applyPendingStateChange();
    [[nodiscard]] std::unique_ptr<State> createState(StateId stateId);

    StateContext& m_context;
    std::unique_ptr<State> m_currentState;
    std::optional<StateId> m_pendingState;
};
