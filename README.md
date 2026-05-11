# Dark Souls V

`Dark Souls V` is a small C++ graphical mini-game created for the Advanced Programming course project requirement.

## What it does

- Native Windows desktop game written in `C++`.
- Pure Win32 + GDI rendering, so it does not rely on Qt, SFML, SDL, or external art assets.
- Complete flow with menu, gameplay, pause, game over, and restart.
- Includes `Normal` and `Hard` modes with clearly different gameplay parameters.
- Random ring relic power-up grants `5` seconds of infinite energy on pickup.
- Enemies now use a sprite model and can occasionally fire projectile attacks.
- The player character uses a transparent knight sprite instead of a simple circle.
- Pulse activation now renders a single flaming sword sweep with a circular fire trail.
- Player survives meteor waves, collects energy shards, and uses a pulse skill to clear nearby threats.

## Controls

- `Left` / `Right` or `1` / `2`: choose mode in menu
- `WASD` or arrow keys: move
- `Space`: release pulse
- `Shift`: focus movement for tighter control
- `Esc` or `P`: pause / resume
- `Enter`: start game
- `R` or `Enter`: restart after game over
- `M`: return to menu after game over

## Build

This repository was prepared for the local environment where `g++` from MSYS2 is available.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The executable will be generated at:

```text
build/Dark Souls V.exe
```

## Project Highlights

- Uses object-oriented structure with a central `Game` class and separate gameplay entities.
- Includes dynamic difficulty scaling for a better gameplay curve.
- Uses mode-based configuration to separate standard play from a harder challenge variant.
- Suitable for a course demo because the logic, rendering, and interaction are all visible and easy to explain.

## Suggested Report Angles

- Why native Win32 was chosen: no third-party dependencies, easier to run on Windows lab machines.
- Core modules: input handling, game loop, collision detection, rendering, state management.
- Creative point: pulse mechanic adds risk management instead of simple endless dodging.
- AI disclosure: if you submit the report, remember to state that AI assisted with code development/document drafting if applicable.
