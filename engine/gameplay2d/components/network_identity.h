/**************************************************************************/
/*  network_identity.h                                                    */
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
// engine/gameplay2d/components/network_identity.h
//
// Public replication-ready identity for entities. Data-only — the demo does
// not run a network stack, but the same shape lets a downstream user wire
// their own multiplayer layer (or the proprietary FateServer) without
// renaming components.
//
// networkId is assigned by the authority (server) when the entity is
// replicated; locally-spawned entities default to 0 and the authority bit
// determines who simulates them.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>
#include <string>

namespace fate {

enum class NetworkAuthority : uint8_t {
    Local  = 0,    // this peer simulates
    Server = 1,    // server simulates, client mirrors
    Owner  = 2,    // peer that owns the entity simulates
};

struct NetworkIdentity {
    FATE_COMPONENT_COLD(NetworkIdentity)

    uint64_t          networkId = 0;             // 0 = unassigned
    uint64_t          ownerPeer = 0;
    NetworkAuthority  authority = NetworkAuthority::Local;
    std::string       sceneId;                   // logical scene/zone tag
    bool              persistent = false;
};

} // namespace fate

FATE_REFLECT(fate::NetworkIdentity,
    FATE_FIELD(networkId, UInt),
    FATE_FIELD(ownerPeer, UInt),
    FATE_FIELD(authority, Enum),
    FATE_FIELD(sceneId, String),
    FATE_FIELD(persistent, Bool)
)
