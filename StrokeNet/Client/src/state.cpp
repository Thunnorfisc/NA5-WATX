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
