#pragma once
#include "engine/render/sdf_font.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace fate {

class FontRegistry {
public:
    static FontRegistry& instance() {
        static FontRegistry s_instance;
        return s_instance;
    }

    // Parse the JSON manifest and create SDFFont entries (no GPU resources).
    // Returns true if the manifest was parsed successfully.
    bool parseManifest(const std::string& jsonPath);

    // Load atlas textures for all registered fonts (call after GL context ready).
    void loadAtlases();

    // Lookup a font by name. Returns nullptr if not found.
    SDFFont* getFont(const std::string& name);

    // Return the "default" font entry, or nullptr if none registered.
    SDFFont* defaultFont();

    // Return a list of all registered font names.
    std::vector<std::string> fontNames() const;

    // Return unique family names across all registered fonts.
    std::vector<std::string> families() const;

    // Return weight variants available for a given family (empty if unknown).
    std::vector<std::string> weightsForFamily(const std::string& family) const;

    // Lookup by family + weight. Returns nullptr if no match.
    SDFFont* getByFamilyWeight(const std::string& family, const std::string& weight);

    // Clear all registered fonts.
    void clear();

private:
    FontRegistry() = default;

    // Parse a single MSDF font entry's metrics JSON into the SDFFont.
    void parseMSDFMetrics(SDFFont& font, const std::string& metricsPath);

    std::unordered_map<std::string, std::unique_ptr<SDFFont>> fonts_;

    // Store atlas paths for deferred loading.
    std::unordered_map<std::string, std::string> atlasPaths_;
};

} // namespace fate
