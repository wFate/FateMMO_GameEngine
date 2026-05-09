/**************************************************************************/
/*  interactable.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
// engine/gameplay2d/components/interactable.h
//
// Public interaction site — "press F when in range to fire actionId". The
// Interaction2DSystem finds the nearest enabled Interactable within
// interactionRadius of the local player and exposes a one-shot trigger flag
// the demo polls during onUpdate.
//
// repeatable=false locks the site after one fire (NPC quest hand-in pattern);
// the system writes consumed_=true on the first hit and ignores it thereafter.
// Callers can manually reset by clearing consumed_ from the inspector.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <string>

namespace fate {

struct Interactable {
    FATE_COMPONENT(Interactable)
    // NOTE: `enabled` is provided by FATE_COMPONENT — do not redeclare it here.

    std::string prompt        = "Press F to interact";
    std::string actionId;          // user-defined; demo logs/echoes
    std::string dialogueId;        // optional dialogue tree key
    float       interactionRadius = 48.0f;
    bool        repeatable = true;
    bool        consumed_  = false;     // runtime, not authored
    bool        triggeredThisFrame_ = false;
};

} // namespace fate

FATE_REFLECT(fate::Interactable,
    FATE_FIELD(prompt, String),
    FATE_FIELD(actionId, String),
    FATE_FIELD(dialogueId, String),
    FATE_FIELD(interactionRadius, Float),
    FATE_FIELD(repeatable, Bool)
)
