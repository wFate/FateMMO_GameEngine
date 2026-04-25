#include "engine/render/font_registry.h"
#include "engine/render/texture.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <set>
#include "stb_image.h"

using json = nlohmann::json;

namespace fate {

namespace {

// Canonical weight tokens. Order roughly matches typographic weight ascending.
struct WeightToken { const char* canonical; const char* lowercase; };
constexpr WeightToken kWeightTokens[] = {
    {"Thin",       "thin"},
    {"ExtraLight", "extralight"},
    {"Light",      "light"},
    {"Regular",    "regular"},
    {"Medium",     "medium"},
    {"SemiBold",   "semibold"},
    {"Bold",       "bold"},
    {"ExtraBold",  "extrabold"},
    {"Black",      "black"},
};

// Split a name like "inter_semibold" or "Inter-Bold" into (family, weight).
// If no known weight suffix is found, family = full name and weight = "Regular".
void autoDeriveFamilyWeight(const std::string& name, std::string& family, std::string& weight) {
    std::string lower(name.size(), '\0');
    std::transform(name.begin(), name.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    size_t sep = lower.find_last_of("_-");
    if (sep != std::string::npos && sep + 1 < lower.size()) {
        std::string tail = lower.substr(sep + 1);
        for (const auto& tok : kWeightTokens) {
            if (tail == tok.lowercase) {
                family = name.substr(0, sep);
                weight = tok.canonical;
                return;
            }
        }
    }
    family = name;
    weight = "Regular";
}

} // namespace

bool FontRegistry::parseManifest(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_ERROR("FontRegistry", "Cannot open manifest: %s", jsonPath.c_str());
        return false;
    }

    json root;
    try {
        root = json::parse(file);
    } catch (const json::parse_error& e) {
        LOG_ERROR("FontRegistry", "JSON parse error in %s: %s", jsonPath.c_str(), e.what());
        return false;
    }

    if (!root.contains("fonts") || !root["fonts"].is_array()) {
        LOG_ERROR("FontRegistry", "Manifest missing 'fonts' array");
        return false;
    }

    for (const auto& entry : root["fonts"]) {
        std::string name = entry.value("name", std::string());
        std::string typeStr = entry.value("type", std::string("msdf"));
        std::string atlasPath = entry.value("atlas", std::string());
        std::string metricsPath = entry.value("metrics", std::string());

        if (name.empty()) {
            LOG_WARN("FontRegistry", "Skipping font entry with no name");
            continue;
        }

        auto font = std::make_unique<SDFFont>();
        font->name = name;

        // Explicit family/weight override the auto-derived values when present.
        std::string explicitFamily = entry.value("family", std::string());
        std::string explicitWeight = entry.value("weight", std::string());
        if (!explicitFamily.empty() || !explicitWeight.empty()) {
            font->family = explicitFamily.empty() ? name : explicitFamily;
            font->weight = explicitWeight.empty() ? "Regular" : explicitWeight;
        } else {
            autoDeriveFamilyWeight(name, font->family, font->weight);
        }

        if (typeStr == "bitmap") {
            font->type = SDFFont::Type::Bitmap;
            font->glyphWidth  = entry.value("glyphWidth", 0);
            font->glyphHeight = entry.value("glyphHeight", 0);
            font->columns     = entry.value("columns", 16);
            font->firstChar   = entry.value("firstChar", 32);
            font->lastChar    = entry.value("lastChar", 126);
        } else {
            font->type = SDFFont::Type::MSDF;
            // Parse the MSDF metrics JSON to populate glyphs and font metrics.
            if (!metricsPath.empty()) {
                parseMSDFMetrics(*font, metricsPath);
            }
        }

        atlasPaths_[name] = atlasPath;
        fonts_[name] = std::move(font);

        LOG_DEBUG("FontRegistry", "Registered font '%s' (type=%s)",
                  name.c_str(), typeStr.c_str());
    }

    LOG_INFO("FontRegistry", "Parsed manifest: %zu font(s)", fonts_.size());
    return true;
}

void FontRegistry::parseMSDFMetrics(SDFFont& font, const std::string& metricsPath) {
    std::ifstream file(metricsPath);
    if (!file.is_open()) {
        LOG_ERROR("FontRegistry", "Cannot open metrics: %s", metricsPath.c_str());
        return;
    }

    json root;
    try {
        root = json::parse(file);
    } catch (const json::parse_error& e) {
        LOG_ERROR("FontRegistry", "JSON parse error in %s: %s", metricsPath.c_str(), e.what());
        return;
    }

    // Atlas metadata
    bool yOriginBottom = false;
    if (root.contains("atlas")) {
        const auto& atlas = root["atlas"];
        font.atlasWidth  = atlas.value("width",  512.0f);
        font.atlasHeight = atlas.value("height", 512.0f);
        font.pxRange     = atlas.value("distanceRange", 4.0f);
        std::string yOrig = atlas.value("yOrigin", std::string("top"));
        yOriginBottom = (yOrig == "bottom");
    }

    // Font metrics
    if (root.contains("metrics")) {
        const auto& metrics = root["metrics"];
        font.emSize     = metrics.value("emSize",     48.0f);
        font.lineHeight = metrics.value("lineHeight", 1.2f);
        font.ascender   = metrics.value("ascender",   0.95f);
    }

    // Glyphs
    if (root.contains("glyphs")) {
        for (const auto& g : root["glyphs"]) {
            uint32_t unicode = g.value("unicode", 0u);
            float advance    = g.value("advance", 0.0f);

            GlyphMetrics gm{};
            gm.advance = advance;

            if (g.contains("planeBounds")) {
                const auto& pb = g["planeBounds"];
                float pbLeft   = pb.value("left",   0.0f);
                float pbBottom = pb.value("bottom", 0.0f);
                float pbRight  = pb.value("right",  0.0f);
                float pbTop    = pb.value("top",    0.0f);

                gm.bearingX = pbLeft;
                gm.bearingY = pbTop;
                gm.width    = pbRight - pbLeft;
                gm.height   = pbTop - pbBottom;
            }

            if (g.contains("atlasBounds")) {
                const auto& ab = g["atlasBounds"];
                float abLeft   = ab.value("left",   0.0f);
                float abBottom = ab.value("bottom", 0.0f);
                float abRight  = ab.value("right",  0.0f);
                float abTop    = ab.value("top",    0.0f);

                gm.uvX = abLeft / font.atlasWidth;
                gm.uvW = (abRight - abLeft) / font.atlasWidth;
                gm.uvH = (abTop - abBottom) / font.atlasHeight;
                if (yOriginBottom) {
                    gm.uvY = (font.atlasHeight - abTop) / font.atlasHeight;
                } else {
                    gm.uvY = abBottom / font.atlasHeight;
                }
            }

            font.glyphs[unicode] = gm;
        }
    }
}

void FontRegistry::loadAtlases() {
    for (auto& [name, font] : fonts_) {
        auto it = atlasPaths_.find(name);
        if (it == atlasPaths_.end() || it->second.empty()) {
            LOG_WARN("FontRegistry", "No atlas path for font '%s'", name.c_str());
            continue;
        }

        // Font atlases must NOT be flipped — UVs are in image-space (y-down).
        // Texture::loadFromFile always flips for GL, so we load manually.
        stbi_set_flip_vertically_on_load(false);
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(it->second.c_str(), &w, &h, &channels, 4);
        stbi_set_flip_vertically_on_load(true); // restore for other textures
        if (!data) {
            LOG_ERROR("FontRegistry", "Failed to load atlas for '%s': %s",
                      name.c_str(), it->second.c_str());
            continue;
        }

        auto tex = std::make_shared<Texture>();
        if (tex->loadFromMemory(data, w, h, 4)) {
            tex->setFilter(true); // linear filtering for SDF text
            font->atlas = tex;
            LOG_DEBUG("FontRegistry", "Loaded atlas for '%s': %s",
                      name.c_str(), it->second.c_str());
        } else {
            LOG_ERROR("FontRegistry", "Failed to create texture for '%s'",
                      name.c_str());
        }
        stbi_image_free(data);
    }
}

SDFFont* FontRegistry::getFont(const std::string& name) {
    auto it = fonts_.find(name);
    if (it != fonts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

SDFFont* FontRegistry::defaultFont() {
    return getFont("default");
}

std::vector<std::string> FontRegistry::fontNames() const {
    std::vector<std::string> names;
    names.reserve(fonts_.size());
    for (const auto& [name, _] : fonts_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> FontRegistry::families() const {
    std::set<std::string> unique;
    for (const auto& [_, font] : fonts_) {
        if (!font->family.empty()) unique.insert(font->family);
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

std::vector<std::string> FontRegistry::weightsForFamily(const std::string& family) const {
    // Canonical weight ordering — return in typographic order, not insertion order.
    std::set<std::string> present;
    for (const auto& [_, font] : fonts_) {
        if (font->family == family) present.insert(font->weight);
    }
    std::vector<std::string> ordered;
    ordered.reserve(present.size());
    for (const auto& tok : kWeightTokens) {
        if (present.count(tok.canonical)) ordered.push_back(tok.canonical);
    }
    // Include anything unrecognised at the end for forward compat.
    for (const auto& w : present) {
        bool known = false;
        for (const auto& tok : kWeightTokens)
            if (w == tok.canonical) { known = true; break; }
        if (!known) ordered.push_back(w);
    }
    return ordered;
}

SDFFont* FontRegistry::getByFamilyWeight(const std::string& family, const std::string& weight) {
    for (const auto& [_, font] : fonts_) {
        if (font->family == family && font->weight == weight) return font.get();
    }
    return nullptr;
}

void FontRegistry::clear() {
    fonts_.clear();
    atlasPaths_.clear();
}

} // namespace fate
