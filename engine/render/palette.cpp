/**************************************************************************/
/*  palette.cpp                                                           */
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
#include "engine/render/palette.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace fate {

bool PaletteRegistry::loadFromJson(const std::string& jsonStr) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        for (auto& [name, def] : j.items()) {
            Palette palette;
            for (const auto& colorStr : def["colors"]) {
                palette.push_back(Color::fromHex(colorStr.get<std::string>()));
            }
            palettes_[name] = std::move(palette);
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[Palette] Parse failed: {}", e.what());
        return false;
    }
}

bool PaletteRegistry::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromJson(content);
}

bool PaletteRegistry::has(const std::string& name) const {
    return palettes_.count(name) > 0;
}

const PaletteRegistry::Palette* PaletteRegistry::get(const std::string& name) const {
    auto it = palettes_.find(name);
    return it != palettes_.end() ? &it->second : nullptr;
}

std::vector<std::string> PaletteRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(palettes_.size());
    for (const auto& [name, _] : palettes_) result.push_back(name);
    return result;
}

} // namespace fate
