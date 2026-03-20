# Project Structure
- Client
    | - *.vcxproj (visual studio project)
    | - x64 (compiler intermediary data)
    | - src
        | - game.cpp
        | - game.hpp
        | - main.cpp

- Server
    | - *.vcxproj (visual studio project)
    | - x64 (compiler intermediary data)
    | - src
        | - main.cpp

- bin
    | - Release
    |   | - resources (Game resource folder)
    |   | - client/server.exe
    |   | - *.pdb
    |   | - *.dll
    |    
    | - Debug
    |   | - resources (Game resource folder)
    |   | - client/server.exe
    |   | - *.pdb
    |   | - *.dll
    
- extern
    | - SFML-3.0.2

- resources (Game resource folder)
