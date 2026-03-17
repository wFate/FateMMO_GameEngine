#pragma once

#if defined(ENGINE_MEMORY_DEBUG)

namespace fate {

class FrameArena;

// Call once per frame from Editor::renderUI()
void drawMemoryPanel(bool* open, FrameArena* frameArena);

} // namespace fate

#endif // ENGINE_MEMORY_DEBUG
