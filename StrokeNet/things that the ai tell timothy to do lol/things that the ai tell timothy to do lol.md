# Foundation Agenda

This is the next layer to build after basic server connectivity.

Right now the project has:

- a server that can listen on UDP and register/unregister clients
- a client gameplay state that already calls `tryGetGameState()` and `sendInputState(...)`
- a newly shared protocol/model contract in `Shared/include/strokenet_protocol.hpp`

The highest-leverage goal now is to turn networking into a stable gameplay boundary so the rest of the team can work against it without touching sockets directly.

## Immediate Milestone

Build a thin authoritative snapshot pipeline:

1. Client owns current `InputState`.
2. Client network thread sends latest `InputState` to server on a fixed cadence.
3. Server stores latest `InputState` per connected session.
4. Server game loop runs at a fixed tick and produces one authoritative `GameState`.
5. Server broadcasts latest `GameState` snapshot back to all clients.
6. Client render/gameplay code only reads `GameState`.

If that loop works, gameplay, UI, and art integration can all start before the final game rules are finished.

## Next Work Items

### 1. Finish the client network service

Add the real networking behavior behind `Client/src/network.cpp`:

- send `REQ_REGISTER` during startup
- keep the assigned session id
- send `MSG_INPUT_STATE` on a fixed interval
- receive `MSG_GAME_STATE`
- expose the latest received `GameState` through `tryGetGameState()`

This keeps the rest of the client code socket-free.

### 2. Add the server fixed-tick game loop

The server currently has a receive thread. Add a second loop that runs at a fixed step:

- read the latest input for every connected player
- update one authoritative game state
- write one fresh snapshot for broadcast

Do not put real drawing-game rules here yet if that slows you down. A dummy loop that updates player cursors and phases is enough to unblock the team.

### 3. Lock down the minimum gameplay contract

The shared protocol already defines:

- session ids
- player roles
- connection state
- round phases
- input snapshot shape
- game snapshot shape

Treat this as the source of truth for cross-team integration. If someone needs more data, add it there first instead of inventing one-off structs inside client or server code.

### 4. Ship a fake-but-playable vertical slice

Before full rules, get this working:

- players can join the lobby
- one player is marked as drawer
- all clients can see cursor positions
- drawer cursor is flagged visible
- server advances phase from `Lobby` to `Drawing`
- clients render from snapshot data only

That is enough for UI and gameplay teammates to start building meaningful features.

### 5. Add observability early

Add lightweight debug support while the system is still small:

- print session id, phase, and player count on both client and server
- show snapshot sequence and server tick on the client
- detect stale connection or missing snapshots

This will save a lot of time once multiple clients are running on LAN.

## Recommended Team Split

These can happen in parallel once the shared contract exists:

- Networking owner
  Finish serialization, packet send/receive, registration, and latest-state handoff.
- Server/gameplay owner
  Build the fixed-tick loop and authoritative state update.
- Client/gameplay owner
  Render and react entirely from `GameState`.
- UI/input owner
  Populate `InputState` from mouse, buttons, and text entry.

## Definition Of "Groundwork Done"

Consider the foundation complete when:

- a client can register and receive a stable session id
- the server maintains latest input for each connected player
- the server publishes `GameState` snapshots at a fixed cadence
- the client renders entirely from server snapshots
- gameplay teammates can change rules by editing the authoritative update step, not the transport layer
