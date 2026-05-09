/**************************************************************************/
/*  atomic_write.h                                                        */
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
#include <string>
#include <string_view>

namespace fate {

// Write `content` to `path` atomically: stream into `<path>.tmp`, then rename
// over the destination. On rename failure, fall back to copy+remove and clean
// up the tmp file. Creates the parent directory if it does not exist.
//
// On any error, the destination file is left untouched (never half-written or
// truncated) and the tmp file is removed. Returns false on failure; if
// `errorOut` is non-null, it receives a short human-readable diagnostic.
//
// Used by the editor's authored-asset save paths (scenes, UI screens, dialogue
// trees, animation templates / metadata). Deliberately bypasses IAssetSource —
// the editor only ever runs against loose-files on disk in non-shipping
// builds, and the .tmp+rename trick has no PhysFS analogue.
bool writeFileAtomic(const std::string& path,
                     std::string_view content,
                     std::string* errorOut = nullptr);

} // namespace fate
