# AGENTS.md

This file provides additional technical guidance for AI agents and automated tools when working with the Snipes codebase.

## Quick Reference for Agents

### Code Style Guidelines
- Follow existing code conventions in each file
- Use tabs for indentation in C++ files
- Maintain 80-character line width where practical
- Preserve original game logic exactly when making fixes

### Platform-Specific Considerations

#### macOS
- SDL operations must run on the main thread
- Use `SDL_MAIN_HANDLED` macro
- Font fallback system searches system fonts if custom font unavailable

#### Windows
- Both SDL and console builds must remain functional
- Visual Studio solution supports multiple configurations
- Console build uses Windows-specific APIs in `windows/` directory

#### Linux/POSIX
- SDL build is primary target
- Ensure POSIX compliance for portability
- Test with various distributions when possible

### Testing Guidelines
- Preserve 100% game logic compatibility with original
- Test both windowed and fullscreen modes
- Verify replay file compatibility
- Check keyboard input with different layouts

### Common Pitfalls to Avoid
- Don't modify core game logic in Snipes.cpp unless fixing documented bugs
- Maintain platform abstraction layer boundaries
- Keep SDL-specific code in `sdl/` directory
- Don't break Windows console build when adding SDL features