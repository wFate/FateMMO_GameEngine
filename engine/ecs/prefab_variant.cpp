/**************************************************************************/
/*  prefab_variant.cpp                                                    */
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
#include "engine/ecs/prefab_variant.h"

namespace fate {

nlohmann::json applyPrefabPatches(const nlohmann::json& base,
                                   const nlohmann::json& patches) {
    return base.patch(patches);
}

nlohmann::json computePrefabDiff(const nlohmann::json& base,
                                  const nlohmann::json& modified) {
    return nlohmann::json::diff(base, modified);
}

} // namespace fate
