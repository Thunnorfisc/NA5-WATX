# Client Architecture

This client is intentionally small.
It is not meant to be a full engine.
The goal is to keep the code easy to change during a two-week project.

## Start Here

If you only remember one thing, remember this:

- Put menu or gameplay behavior inside the current state class.
- Put drawing inside that state's `render()`.
- Put per-frame logic inside that state's `update()`.
- Put click/key handling inside that state's `handleEvent()`.
- Put window/app loop code in `Game`.
- Put screen switching in `requestStateChange(...)`.
- Keep gameplay networking in `CoreGameState`, not in shared context.

In practice:

- If you want to change main menu behavior, edit `src/mainmenu.cpp`.
- If you want to change gameplay behavior, edit `src/coregame.cpp`.
- If you want to change the main loop, window settings, or top-level event flow, edit `src/game.cpp`.
- If you want to add a new screen, create a new `State` subclass and register it in `src/state_machine.cpp`.
- If you think "should this go in shared context?", only do that when multiple states truly need it.

## What Goes Where

Use this as the quick rulebook:

- `Game`
  Owns the app.
  Creates the window.
  Runs the loop.
  Polls SFML events.
  Calls update/render on the current state.
- `StateMachine`
  Owns which screen is active.
  Creates states.
  Switches from one state to another.
- `State`
  Base class for screens.
  Gives each screen access to shared context and state changes.
- `MainMenuState`
  Put main menu UI and menu behavior here.
- `CoreGameState`
  Put actual gameplay logic here.
  Keep gameplay networking data here too.
- `StateContext`
  Only for data that every state may need, such as the window or current event.

## Typical Workflow

When adding or changing features, this is usually the right place:

- Add gameplay rules, timers, movement, combat, round logic, or networked gameplay behavior in `CoreGameState::update()`.
- React to key presses, mouse clicks, or other SFML events in `handleEvent()`.
- Draw text, shapes, sprites, and UI in `render()`.
- Move to another screen with `requestStateChange(StateId::...)`.
- Read the window or mouse coordinates through `context().window`.
- Keep per-state members inside that state class instead of making globals.

## Mental Model

Think of the client like this:

- `Game` runs the program.
- `StateMachine` picks which screen is alive.
- The active `State` does the real work for that screen.

So if you are working on gameplay, you usually should not touch `Game` or `StateMachine`.
You should mostly be editing `CoreGameState`.

## Overview

The client uses a simple `Game -> StateMachine -> State` structure.

- `Game` owns the main window and main loop.
- `StateMachine` owns the current screen/state.
- `State` is the base class for each screen such as main menu or gameplay.
- `StateContext` holds shared data that every state may need.
- `CoreGameState` owns the network gameplay data it needs while gameplay is active.

Right now there is only one active state at a time.
There is no stack, no push/pop state system, and no complex scene framework.
That is on purpose.

## Main Files

- `src/main.cpp`
  Starts the client.
- `src/game.hpp` and `src/game.cpp`
  Own the SFML window, run the loop, collect events, update context, and draw.
- `src/state.hpp` and `src/state.cpp`
  Define the `State` base class and `StateContext`.
- `src/state_machine.hpp` and `src/state_machine.cpp`
  Hold the active state and switch between states.
- `src/mainmenu.hpp` and `src/mainmenu.cpp`
  Main menu state.
- `src/coregame.hpp` and `src/coregame.cpp`
  Gameplay state.
- `src/network.hpp` and `src/network.cpp`
  Network-facing game/input data hooks.

## Runtime Flow

Each frame follows this order:

1. `Game::processEvents()`
   Poll SFML events from the window.
   Close the window if needed.
   Forward events to the current state.
2. `Game::update()`
   Update the active state.
3. `Game::render()`
   Clear the window.
   Ask the active state to draw.
   Display the frame.

This keeps responsibilities clean:

- `Game` handles application-level flow.
- `StateMachine` handles which screen is active.
- Each state handles its own behavior and visuals.

## StateContext

`StateContext` is the shared object passed to every state.

It currently contains:

- `window`
  The SFML render window.
- `event`
  The most recent event being processed this frame.

Use it inside any state through `context()`.

Examples:

- `context().window`
  Draw things, read size, map mouse coordinates.
- `context().event`
  Read the current event if a state needs shared event access beyond the `handleEvent(...)` call.

## State Base Class

Each state derives from `State` and implements:

- `handleEvent(const sf::Event& event)`
  React to input/window events.
- `update(sf::Time deltaTime)`
  Run gameplay or menu logic.
- `render()`
  Draw the state.

Each state can also call:

- `context()`
  Access shared data from `StateContext`.
- `requestStateChange(StateId::...)`
  Ask the state machine to switch screens.

The state itself owns its own UI objects and local data.
For example, button shapes, text, timers, and temporary state should live inside that concrete state class.

## Current States

### `MainMenuState`

Responsibilities:

- Draw the play button.
- Detect click on the play button.
- Request transition to `StateId::CoreGame`.

### `CoreGameState`

Responsibilities:

- Draw basic gameplay screen UI.
- Detect click on the back button.
- Poll `GameState` from networking while gameplay is active.
- Send `InputState` to networking while gameplay is active.
- Request transition to `StateId::MainMenu`.

## How To Add A New State

If you want to add another screen such as `LobbyState` or `PauseState`, follow this pattern:

1. Add a new enum value to `StateId` in `src/state.hpp`.
2. Create `src/yourstate.hpp` and `src/yourstate.cpp`.
3. Make the class derive from `State`.
4. Implement `handleEvent`, `update`, and `render`.
5. Register the state in `StateMachine::createState()` in `src/state_machine.cpp`.
6. Add the new files to `Client.vcxproj` and `Client.vcxproj.filters`.
7. Trigger it from another state with `requestStateChange(StateId::YourState)`.

Minimal example:

```cpp
class LobbyState final : public State
{
public:
    LobbyState(StateMachine& stateMachine, StateContext& context)
        : State(stateMachine, context)
    {
    }

    void handleEvent(const sf::Event& event) override
    {
    }

    void update(sf::Time deltaTime) override
    {
    }

    void render() override
    {
        context().window.draw(...);
    }
};
```

## How To Use Networking In This Client

Networking gameplay data is intentionally not shared across every state.

Right now:

- `CoreGameState` owns `m_gameState`
- `CoreGameState` owns `m_inputState`
- `CoreGameState::update()` calls `tryGetGameState()`
- `CoreGameState::update()` calls `sendInputState(m_inputState)`

This means:

- menus do not know about gameplay networking data
- shared state context stays small
- gameplay-specific network logic stays in gameplay code

If you later add another networked gameplay state, it can own its own network-facing data the same way.

## Rules For This Project

These are good guardrails for this codebase:

- Keep one active state at a time unless a real need appears.
- Put menu/gameplay-specific data inside the state class that uses it.
- Put shared cross-state data into `StateContext`.
- Only put network gameplay data in states that truly need it.
- Prefer adding simple helper functions over building generic systems too early.
- If a feature is only used once, it probably does not need its own manager class.

## When To Extend The Architecture

Only extend this setup if the project actually needs it.

Examples that may justify expansion:

- multiple overlays at once
- pause menus on top of gameplay
- asset loading shared across many states
- audio management shared across many states
- larger UI systems reused in many screens

Until then, this structure should be enough and should stay fast to work in.
