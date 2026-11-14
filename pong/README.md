# Pong

A single-player Pong game written in C++ with SFML 3. You play against an AI opponent with three difficulty levels.

---

## Features

- Three AI difficulty levels (Easy / Medium / Hard)
- Physics-based ball bouncing with angle control off the paddle face
- Procedurally generated sound effects — no audio files required
- Sounds for paddle hits, wall bounces, scoring, victory, and defeat
- Clean game-state flow: difficulty selection → ready → playing → game over
- Winner announcement with final score on the game over screen

## Controls

| Key | Action |
|-----|--------|
| `↑` / `↓` | Move your paddle |
| `1` / `2` / `3` | Select difficulty on the menu |
| `Space` | Start game / Play again |
| `Esc` | Quit (from the Game Over screen) |

First to **5 points** wins.

---

## Building

### Requirements

- CMake ≥ 3.20
- A C++17 compiler (GCC, Clang, MSVC)
- [SFML 3](https://www.sfml-dev.org/) installed (Graphics, Window, System, Audio)

On macOS with Homebrew:

```bash
brew install sfml
```

### Build steps

```bash
cd pong
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running

```bash
cd build
./pong
```

The binary must be run from the `build/` directory (or any directory that contains an `assets/` folder next to the executable). CMake copies `assets/` there automatically after each build.

---

## Project structure

```
pong/
├── assets/
│   └── fonts/
│       └── game.ttf        # Bitmap game font
├── build/                  # CMake build output (generated)
├── fonts/                  # Original font location (kept for reference)
├── Ai.cpp / Ai.hpp         # AI opponent logic
├── Ball.cpp / Ball.hpp     # Ball physics and collision
├── Paddle.cpp / Paddle.hpp # Paddle movement and bounds
├── globals.hpp             # Shared window/paddle constants
├── main.cpp                # Game loop, state machine, rendering, audio
├── CMakeLists.txt
└── README.md
```
