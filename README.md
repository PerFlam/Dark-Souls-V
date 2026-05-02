# Pulse Harbor

`Pulse Harbor` is a small C++ graphical mini-game created for the Advanced Programming course project requirement.

## What it does

- Native Windows desktop game written in `C++`.
- Pure Win32 + GDI rendering, so it does not rely on Qt, SFML, SDL, or external art assets.
- Complete flow with menu, gameplay, pause, game over, and restart.
- Player survives meteor waves, collects energy shards, and uses a pulse skill to clear nearby threats.

## Controls

- `WASD` or arrow keys: move
- `Space`: release pulse
- `Shift`: focus movement for tighter control
- `Esc` or `P`: pause / resume
- `Enter`: start game
- `R` or `Enter`: restart after game over

## Build

This repository was prepared for the local environment where `g++` from MSYS2 is available.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The executable will be generated at:

```text
build/PulseHarbor.exe
```

## Project Highlights

- Uses object-oriented structure with a central `Game` class and separate gameplay entities.
- Includes dynamic difficulty scaling for a better gameplay curve.
- Suitable for a course demo because the logic, rendering, and interaction are all visible and easy to explain.

## Suggested Report Angles

- Why native Win32 was chosen: no third-party dependencies, easier to run on Windows lab machines.
- Core modules: input handling, game loop, collision detection, rendering, state management.
- Creative point: pulse mechanic adds risk management instead of simple endless dodging.
- AI disclosure: if you submit the report, remember to state that AI assisted with code development/document drafting if applicable.
