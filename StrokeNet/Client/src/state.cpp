/* Start Header
***********************************************************************/

/*! \file   state.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "state.hpp"

#include "state_machine.hpp"

State::State(StateMachine& stateMachine, StateContext& context) :
    m_stateMachine(stateMachine),
    m_context(context)
{
}

void State::handleEvent(const sf::Event&)
{
}

void State::requestStateChange(StateId stateId)
{
    m_stateMachine.changeState(stateId);
}

StateContext& State::context()
{
    return m_context;
}

const StateContext& State::context() const
{
    return m_context;
}
