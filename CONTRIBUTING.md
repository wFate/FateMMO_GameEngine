# Contributing to FateEngine

FateEngine is the open-source engine core behind [FateMMO](https://github.com/wFate/FateMMO_GameEngine). Contributions that improve the engine library, editor tooling, build system, and documentation are welcome.

## What's in scope

The public repository contains the **engine core** (`engine/`), demo application (`examples/`), shaders (`assets/shaders/`), UI layouts (`assets/ui/`), and build infrastructure. Contributions should target these areas.

The proprietary game logic (`game/`), server (`server/`), tests (`tests/`), and art assets are not part of the open-source release.

## Reporting issues

Open an issue on GitHub with:

- A clear title describing the problem
- Steps to reproduce (build commands, platform, compiler version)
- Expected vs actual behavior
- Build log or error output if applicable

## Pull requests

1. Fork the repository and create a branch from `main`
2. Make your changes in focused, reviewable commits
3. Ensure the project builds cleanly on at least one CI target (Windows MSVC, Linux GCC 14, or Linux Clang 18)
4. Open a pull request against `main` with a short summary of what changed and why

### Code style

- C++23, consistent with existing engine code
- No tabs -- 4-space indentation
- `camelCase` for functions and variables, `PascalCase` for types, `camelCase_` trailing underscore for member variables
- Keep headers self-contained (include what you use)
- Prefer `#pragma once` for header guards

### FATE_HAS_GAME guards

The open-source build compiles without the `game/` directory. Any `#include "game/..."` added to engine files **must** be wrapped in preprocessor guards:

```cpp
#ifdef FATE_HAS_GAME
#include "game/components/some_component.h"
#endif // FATE_HAS_GAME
```

Code that uses types from those headers must also be inside `#ifdef FATE_HAS_GAME` blocks. The CI build will catch unguarded includes -- both Linux jobs build without `game/` sources.

### Build verification

Before submitting, confirm your changes compile in the demo configuration (no `game/` directory):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target fate_engine
cmake --build build --target FateDemo
```

## What makes a good contribution

- Bug fixes with clear reproduction steps
- Build system improvements (new platform support, dependency updates, CI enhancements)
- Editor UX improvements
- Performance optimizations with before/after measurements
- Fixes for any of the known issues listed in the README

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0, consistent with the project license.
