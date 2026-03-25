/* Start Header
***********************************************************************/

/*! \file   state_machine.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "state_machine.hpp"

#include "coregame.hpp"
#include "mainmenu.hpp"
#include "loginstate.hpp"

StateMachine::StateMachine(StateContext& context) :
    m_context(context)
{
}

void StateMachine::changeState(StateId stateId)
{
    m_pendingState = stateId;

    if (!m_currentState)
    {
        applyPendingStateChange();
    }
}

void StateMachine::handleEvent(const sf::Event& event)
{
    if (m_currentState)
    {
        m_currentState->handleEvent(event);
    }
}

void StateMachine::update(sf::Time deltaTime)
{
    applyPendingStateChange();

    if (m_currentState)
    {
        m_currentState->update(deltaTime);
    }

    applyPendingStateChange();
}

void StateMachine::render()
{
    if (m_currentState)
    {
        m_currentState->render();
    }
}

void StateMachine::applyPendingStateChange()
{
    if (!m_pendingState)
    {
        return;
    }

    m_currentState = createState(*m_pendingState);
    m_pendingState.reset();
}

std::unique_ptr<State> StateMachine::createState(StateId stateId)
{
    switch (stateId)
    {
    case StateId::Login:
        return std::make_unique<LoginState>(*this, m_context);
    case StateId::MainMenu:
        return std::make_unique<MainMenuState>(*this, m_context);
    case StateId::CoreGame:
        return std::make_unique<CoreGameState>(*this, m_context);
    }

    return std::make_unique<MainMenuState>(*this, m_context);
}
