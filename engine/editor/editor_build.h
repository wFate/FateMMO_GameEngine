#pragma once

// EDITOR_BUILD is defined by CMake only for editor-linked targets
// (FateEngine client and fate_tests). It is NOT defined for FateServer
// or shipping mobile builds.
//
// Usage:
//   #ifdef EDITOR_BUILD
//   #include "engine/editor/editor.h"
//   #endif
//
// Editor code in .cpp files should also be guarded:
//   #ifdef EDITOR_BUILD
//   Editor::instance().doSomething();
//   #endif
