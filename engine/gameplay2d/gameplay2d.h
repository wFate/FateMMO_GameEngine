/**************************************************************************/
/*  gameplay2d.h                                                          */
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
// engine/gameplay2d/gameplay2d.h
//
// Umbrella header — pulls in every public component, system, and the
// registration entry point. Convenient single include for the demo / tests
// that want the full public set; granular includes are still preferred from
// the engine library itself to keep build times honest.

#pragma once

// Components
#include "engine/gameplay2d/components/collider2d.h"
#include "engine/gameplay2d/components/trigger_area2d.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/gameplay2d/components/sprite_animator2d.h"
#include "engine/gameplay2d/components/camera_follow2d.h"
#include "engine/gameplay2d/components/health.h"
#include "engine/gameplay2d/components/damageable.h"
#include "engine/gameplay2d/components/attack.h"
#include "engine/gameplay2d/components/targetable.h"
#include "engine/gameplay2d/components/nameplate.h"
#include "engine/gameplay2d/components/interactable.h"
#include "engine/gameplay2d/components/npc2d.h"
#include "engine/gameplay2d/components/zone2d.h"
#include "engine/gameplay2d/components/portal2d.h"
#include "engine/gameplay2d/components/spawn_point2d.h"
#include "engine/gameplay2d/components/spawn_zone2d.h"
#include "engine/gameplay2d/components/network_identity.h"
#include "engine/gameplay2d/components/replicated_transform2d.h"
#include "engine/gameplay2d/components/interest_source.h"
#include "engine/gameplay2d/components/mob2d.h"

// Systems
#include "engine/gameplay2d/systems/character_controller_system.h"
#include "engine/gameplay2d/systems/sprite_animator_system.h"
#include "engine/gameplay2d/systems/camera_follow_system.h"
#include "engine/gameplay2d/systems/trigger_system.h"
#include "engine/gameplay2d/systems/interaction_system.h"
#include "engine/gameplay2d/systems/health_damage_system.h"
#include "engine/gameplay2d/systems/nameplate_render_system.h"
#include "engine/gameplay2d/systems/portal_zone_system.h"
#include "engine/gameplay2d/systems/spawn_zone_system.h"
#include "engine/gameplay2d/systems/targeting_system.h"
#include "engine/gameplay2d/systems/mob2d_system.h"

// Registration entry point
#include "engine/gameplay2d/register_gameplay2d.h"
