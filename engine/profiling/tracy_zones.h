/**************************************************************************/
/*  tracy_zones.h                                                         */
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
#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define FATE_ZONE(name, color) ZoneScopedNC(name, color)
#define FATE_ZONE_NAME(name) ZoneScopedN(name)
#define FATE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define FATE_FREE(ptr) TracyFree(ptr)
#define FATE_FRAME_MARK FrameMark
#else
#define FATE_ZONE(name, color)
#define FATE_ZONE_NAME(name)
#define FATE_ALLOC(ptr, size)
#define FATE_FREE(ptr)
#define FATE_FRAME_MARK
#endif
