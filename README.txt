================================================================================
                        StrokeNet - README
          CSD2161/CSD2160 Computer Networks - 2026 Spring
                  DigiPen Institute of Technology
                    Assignment 5 (Option 2)
================================================================================

StrokeNet is a multiplayer networked drawing and guessing game (Pictionary-
style) played over a LAN using UDP. One player draws a secret word on a shared
canvas while other players guess the word via in-game chat.

================================================================================
TABLE OF CONTENTS
================================================================================

  1. Prerequisites
  2. Building from Source
  3. Running the Game
  4. How to Play
  5. Controls
  6. Game Rules and Scoring
  7. Network Architecture
  8. Project Structure
  9. Troubleshooting
 10. Team Members

================================================================================
1. PREREQUISITES
================================================================================

  - Windows 10/11 (x64)
  - Visual Studio 2022 (with C++ desktop development workload)
  - All required libraries are bundled in the repository:
      * SFML 3.0.2  (extern/SFML-3.0.2/)
      * OpenSSL     (DLLs included in bin/)

================================================================================
2. BUILDING FROM SOURCE
================================================================================

  1. Open the solution file:
       StrokeNet/StrokeNet.sln

  2. In Visual Studio, set the configuration to:
       - Configuration: Release (or Debug)
       - Platform:      x64

  3. Build the solution:
       Build > Build Solution  (or press Ctrl+Shift+B)

  4. The executables will be output to:
       bin/Release/Server.exe
       bin/Release/Client.exe
     (or bin/Debug/ for Debug builds)

  NOTE: All required DLLs (SFML, OpenSSL) and the resources/ folder are
  already present in the bin/Release/ and bin/Debug/ directories. No
  additional setup is needed after building.

================================================================================
3. RUNNING THE GAME
================================================================================

  IMPORTANT: All machines must be on the same LAN (local network).

  Step 1 - Start the Server
  -------------------------
    Navigate to the build output folder and run the server:

       bin/Release/Server.exe       (or bin/Debug/Server.exe)

    The server will:
      - Bind to UDP port 32112 on all network interfaces
      - Listen for incoming client connections
      - Print its IP address and port to the console
      - Wait for players to connect

    No command-line arguments or configuration files are needed. The server
    auto-configures on startup.

  Step 2 - Start the Client(s)
  ----------------------------
    On each player's machine, run:

       bin/Release/Client.exe       (or bin/Debug/Client.exe)

    You can also run multiple clients on the same machine for testing.

  Step 3 - Log In or Create an Account
  -------------------------------------
    On the login screen:
      - Enter a username and password.
      - Click "Login" to log in with existing credentials.
      - Click "Create Account" to register a new account.

    Server discovery:
      - By default, the client broadcasts on the LAN (255.255.255.255:32112)
        to automatically discover the server. No config file is needed.
      - Alternatively, you can enter the server's IP address directly in the
        IP field for a direct connection.

  Step 4 - Join a Game
  --------------------
    After logging in, you will be taken to the Main Menu.
      - Click "Play" to join the game session.
      - The game starts automatically once at least 2 players have joined.

================================================================================
4. HOW TO PLAY
================================================================================

  StrokeNet is a Pictionary-style drawing and guessing game:

  1. DRAWING PHASE
     - Each round, one player is designated as the "drawer."
     - The drawer receives a secret word (shown at the top of their screen).
     - The drawer must draw the word on the canvas using the available tools.
     - All other players see the drawing appear in real time on their screens.

  2. GUESSING PHASE
     - While the drawer is drawing, all other players try to guess the word.
     - Type your guess into the chat box at the bottom of the screen and press
       Enter to submit.
     - If your guess matches the secret word, you and the drawer each earn
       +1 point.
     - Other players see a notification that you guessed correctly (the word
       itself is not revealed to those who haven't guessed yet).

  3. TURN ROTATION
     - After the turn timer expires (or all guessers have guessed correctly),
       the turn advances to the next player.
     - Every player gets a turn to draw in each round.

  4. ROUNDS AND GAME END
     - The game runs for 3 rounds (each player draws once per round).
     - After all rounds are complete, the leaderboard is displayed showing
       the top 5 players' names and high scores.
     - The player with the highest score wins!

  5. LATE JOINING
     - If you join a game that is already in progress, the server will stream
       the current canvas (stroke history) and recent chat messages to you,
       so you can see what has been drawn and said so far.

================================================================================
5. CONTROLS
================================================================================

  DRAWING (when you are the drawer):
  -----------------------------------
    Left Mouse Button (hold + drag)   Draw on the canvas
    Right Mouse Button (hold + drag)  Erase on the canvas
    Colour Palette (left panel)       Click a colour swatch to select it
    Tool Picker                       Click brush/eraser icon to switch tools
    Thickness Slider                  Drag the slider to adjust brush size
                                      (range: 1 - 50 pixels)
    Clear Canvas button               Clears the entire canvas for all players

  CHAT / GUESSING (when you are a guesser):
  ------------------------------------------
    Click the chat input box           Start typing a message
    Type your guess                    Up to 84 characters
    Enter                              Send your message / guess

  GENERAL:
  ---------
    Back button                        Return to the Main Menu
                                       (leaves the current game)

================================================================================
6. GAME RULES AND SCORING
================================================================================

  SCORING:
    - Drawer earns +1 point for each player who guesses correctly.
    - Guesser earns +1 point for a correct guess.
    - Only the first correct guess per player per turn counts (no duplicates).

  ROUNDS:
    - The game consists of 3 rounds.
    - In each round, every connected player takes one turn as the drawer.
    - Turns advance when the timer expires or all guessers guess correctly.

  WIN CONDITION:
    - The player with the highest cumulative score at the end of all rounds
      wins the game.

  PERSISTENCE:
    - User accounts (username + hashed password) persist across sessions.
    - High scores are saved per user in users.json on the server.
    - The top 5 players' names and high scores are displayed on the
      leaderboard at the end of the game.

  WORD LIST:
    - The server loads drawing words from resources/words.txt.
    - Words are selected randomly each turn.
    - The word length is shown as a hint to guessers.

================================================================================
7. NETWORK ARCHITECTURE
================================================================================

  PROTOCOL:        UDP (as required by the assignment)
  PORT:            32112
  ARCHITECTURE:    Client-Server (server-authoritative)
  MAX PLAYERS:     6 per game session (minimum 2 to start)

  The server is the single source of truth for all game state (scores, rounds,
  active drawer, word selection, stroke history). Clients send requests and wait
  for server confirmation before updating local state.

  Three message reliability categories are used:

    1. REQ/RSP (Request/Response)
       - Client sends a request; server replies.
       - Used for: login, account creation, game join/quit, stroke start/end,
         clear canvas, chat messages.
       - Retried every 100ms until ACK received or 1-second timeout.

    2. NTF/NTF_RCV (Notification/Acknowledgment)
       - Server pushes updates; clients acknowledge.
       - Used for: scoreboard, leaderboard, round timer, word distribution,
         stroke broadcasts, history streaming.
       - Same retry/timeout strategy as REQ/RSP.

    3. FAF (Fire-and-Forget)
       - Best-effort, no acknowledgment.
       - Used for: drawing extension updates (sent at 60 Hz - packet loss
         is acceptable since the next update arrives within ~16ms), and
         client disconnect notification.

  DURABLE COMMUNICATION:
    - Minor packet loss is handled by the retry mechanism (100ms intervals).
    - If a player disconnects, other players are not affected.
    - If the drawer disconnects, the turn advances to the next player.
    - Reconnecting players receive full stroke history and chat history
      from the server, allowing them to continue the game.

  SERVER DISCOVERY:
    - Clients can discover the server via LAN broadcast (255.255.255.255)
      on port 32112, eliminating the need for config files.
    - Alternatively, clients can connect via a direct IP address.

================================================================================
8. PROJECT STRUCTURE
================================================================================

  NA5-WATX/
  |
  +-- StrokeNet/
  |   +-- StrokeNet.sln              Visual Studio solution file
  |   |
  |   +-- Client/
  |   |   +-- Client.vcxproj         Client project file
  |   |   +-- src/
  |   |       +-- main.cpp           Client entry point
  |   |       +-- game.hpp/cpp       Main game loop, SFML window
  |   |       +-- client.hpp/cpp     Network client (UDP, listening thread)
  |   |       +-- state.hpp/cpp      State machine base class
  |   |       +-- state_machine.*    State management
  |   |       +-- loginstate.*       Login / account creation UI
  |   |       +-- mainmenu.*         Main menu UI
  |   |       +-- coregame.*         Core gameplay state
  |   |       +-- Canvas.*           Drawing canvas
  |   |       +-- Drawing.*          Stroke rendering
  |   |       +-- ColourPicker.*     Colour palette selector
  |   |       +-- ToolPicker.*       Brush/eraser tool selector
  |   |       +-- chatBox.*          Chat and scoreboard UI
  |   |
  |   +-- Server/
  |   |   +-- Server.vcxproj         Server project file
  |   |   +-- src/
  |   |       +-- main.cpp           Server entry point and game loop
  |   |       +-- server.hpp/cpp     Server core (packet handling, state)
  |   |       +-- login.hpp/cpp      User authentication (SHA-256, JSON)
  |   |
  |   +-- shared/
  |       +-- shared_protocol.hpp    Protocol definitions and message types
  |
  +-- bin/
  |   +-- Release/                   Release build output
  |   |   +-- Server.exe
  |   |   +-- Client.exe
  |   |   +-- *.dll                  SFML + OpenSSL runtime libraries
  |   |   +-- resources/             Game assets (copied at build time)
  |   |   +-- users.json             Persistent user database
  |   |
  |   +-- Debug/                     Debug build output (same structure)
  |
  +-- extern/
  |   +-- SFML-3.0.2/                SFML library (headers + libs)
  |
  +-- resources/                     Source game assets
      +-- fonts/                     Marvel-Bold.ttf, Marvel-Regular.ttf
      +-- sprites/                   UI icons and game art
      +-- words.txt                  Word list for drawing rounds
      +-- bgm.mp3                    Background music
      +-- UI_Hover_v1.wav            UI hover sound effect
      +-- UI_Select_v1.wav           UI selection sound effect

================================================================================
9. TROUBLESHOOTING
================================================================================

  Q: The client cannot find the server.
  A: Ensure both machines are on the same LAN. Try entering the server's IP
     address directly instead of using broadcast. Check that UDP port 32112
     is not blocked by the Windows Firewall. You may need to allow
     Server.exe and Client.exe through the firewall.

  Q: The game does not start after players join.
  A: At least 2 players must click "Play" from the Main Menu. The server
     starts the game automatically once 2+ players are in the game session.

  Q: DLL errors when launching the .exe files.
  A: Make sure the SFML and OpenSSL DLLs are in the same directory as the
     executable. They should already be present in bin/Release/ and
     bin/Debug/.

  Q: Drawing appears laggy or strokes are missing points.
  A: Drawing extension updates use fire-and-forget UDP at 60 Hz. On a
     congested network, some points may be dropped. This is expected
     behavior - the overall stroke shape should still be recognizable.

  Q: A player disconnected and the game froze.
  A: The game should continue normally for remaining players. If the
     disconnected player was the drawer, the turn advances automatically.
     The disconnected player can reconnect by logging in again.

================================================================================
10. TEAM MEMBERS
================================================================================

  [TODO: Fill in your team members' details]

  Full Name              	SIT ID         	DigiPen ID      	Contribution
  ---------------------  	-------------  	-----------------  	------------
  Loh Boon Cheong, Timothy      2401679         loh.b           	25%
  William Wibisana Dumanauw     2401252         williamwibisana.d       25%
  Alfred Lo Kai Xuan            2400551         alfredkaixuan.lo        25%
  Xavier Koh Zhi Kuang          2401453         z.koh          		25%

================================================================================
                          End of README
================================================================================
