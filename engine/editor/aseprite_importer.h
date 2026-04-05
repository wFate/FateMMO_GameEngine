#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace fate {

struct ImportedState {
    std::string name;           // "idle", "walk", "attack"
    std::string direction;      // "down", "up", "left", "right" (side already split)
    int startFrame = 0;         // absolute index in sheet
    int frameCount = 1;
    float frameRate = 8.0f;     // Hz, used when durations are uniform
    std::vector<int> frameDurationsMs; // per-frame ms, empty = uniform
    int hitFrame = -1;          // -1 = none, 0-based within state
    bool loop = true;
    bool flipX = false;         // true for _left entries synthesized from _side
};

struct AsepriteImportResult {
    int frameWidth = 0, frameHeight = 0;
    int columns = 0, rows = 0;
    int totalFrames = 0;
    std::vector<ImportedState> states;
    std::string sheetPath;
    std::vector<std::string> warnings;
};

class AsepriteImporter {
public:
    // Parse a single Aseprite JSON (in-memory). Used by tests and by parse().
    static std::optional<AsepriteImportResult> parseJson(const nlohmann::json& json);

    // Merge sibling results (front/back/side) into one combined result.
    static AsepriteImportResult mergeSiblings(
        const AsepriteImportResult& primary,
        const std::optional<AsepriteImportResult>& back,
        const std::optional<AsepriteImportResult>& side);

    // Parse from file path. Auto-discovers _front/_back/_side siblings and merges.
    static std::optional<AsepriteImportResult> parse(const std::string& jsonPath);
};

} // namespace fate
