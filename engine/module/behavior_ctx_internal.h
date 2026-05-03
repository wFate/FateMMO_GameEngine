#pragma once
// engine/module/behavior_ctx_internal.h
//
// Concrete shape behind the C ABI's opaque `FateBehaviorCtx*`. Used by:
//   - engine/module/hot_reload_manager.cpp (the only producer of ctxs)
//   - tests that drive vtables in-process without going through LoadLibrary
//
// NOT included by the runtime DLL — modules must continue to treat
// FateBehaviorCtx as opaque (per the contract in fate_module_abi.h). Defining
// the struct here at engine scope rather than ABI scope is the trick that
// keeps the public ABI honest.

#include "engine/module/fate_module_abi.h"

namespace fate {
class World;
class Entity;
struct BehaviorComponent;
}

extern "C" struct FateBehaviorCtx {
    fate::World*              world;
    fate::Entity*             entity;
    fate::BehaviorComponent*  component;
    void*                     state;
};
