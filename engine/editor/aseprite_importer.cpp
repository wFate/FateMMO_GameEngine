#include "engine/editor/aseprite_importer.h"
#include "engine/core/logger.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_set>

namespace fate {

namespace {

// Split tag name on the last '_' to extract state name and direction.
// Valid direction suffixes: "down", "up", "side".
// Tags without a recognized suffix default to direction "down".
void splitTagName(const std::string& tagName, std::string& outState, std::string& outDir) {
    auto pos = tagName.rfind('_');
    if (pos != std::string::npos && pos > 0) {
        std::string suffix = tagName.substr(pos + 1);
        if (suffix == "down" || suffix == "up" || suffix == "side") {
            outState = tagName.substr(0, pos);
            outDir = suffix;
            return;
        }
    }
    // No recognized direction suffix -- entire name is the state, default to "down"
    outState = tagName;
    outDir = "down";
}

// Determine loop default based on state name.
bool inferLoop(const std::string& stateName) {
    static const std::unordered_set<std::string> noLoop = {
        "attack", "cast", "death", "hit", "hurt", "die", "skill"
    };
    return noLoop.find(stateName) == noLoop.end();
}

} // anonymous namespace

std::optional<AsepriteImportResult> AsepriteImporter::parseJson(const nlohmann::json& j) {
    // -------------------------------------------------------------------------
    // 1. Extract frames -- support both array and hash formats
    // -------------------------------------------------------------------------
    if (!j.contains("frames")) {
        LOG_ERROR("AsepriteImporter", "JSON missing 'frames' key");
        return std::nullopt;
    }

    struct FrameInfo {
        int x, y, w, h;
        int sourceW, sourceH;
        int durationMs;
    };
    std::vector<FrameInfo> frames;

    const auto& framesNode = j["frames"];
    if (framesNode.is_array()) {
        frames.reserve(framesNode.size());
        for (const auto& f : framesNode) {
            FrameInfo fi;
            const auto& rect = f["frame"];
            fi.x = rect["x"].get<int>();
            fi.y = rect["y"].get<int>();
            fi.w = rect["w"].get<int>();
            fi.h = rect["h"].get<int>();
            const auto& ss = f["sourceSize"];
            fi.sourceW = ss["w"].get<int>();
            fi.sourceH = ss["h"].get<int>();
            fi.durationMs = f["duration"].get<int>();
            frames.push_back(fi);
        }
    } else if (framesNode.is_object()) {
        // Hash format: keys are filenames, values are frame objects.
        // Aseprite orders them by filename, so we preserve insertion order
        // (nlohmann::json::object_t is ordered_map by default in v3.11+).
        for (const auto& [key, f] : framesNode.items()) {
            FrameInfo fi;
            const auto& rect = f["frame"];
            fi.x = rect["x"].get<int>();
            fi.y = rect["y"].get<int>();
            fi.w = rect["w"].get<int>();
            fi.h = rect["h"].get<int>();
            const auto& ss = f["sourceSize"];
            fi.sourceW = ss["w"].get<int>();
            fi.sourceH = ss["h"].get<int>();
            fi.durationMs = f["duration"].get<int>();
            frames.push_back(fi);
        }
    } else {
        LOG_ERROR("AsepriteImporter", "'frames' is neither array nor object");
        return std::nullopt;
    }

    if (frames.empty()) {
        LOG_ERROR("AsepriteImporter", "No frames found");
        return std::nullopt;
    }

    AsepriteImportResult result;

    // -------------------------------------------------------------------------
    // 2. Derive frame dimensions from first frame's sourceSize
    // -------------------------------------------------------------------------
    result.frameWidth = frames[0].sourceW;
    result.frameHeight = frames[0].sourceH;
    result.totalFrames = static_cast<int>(frames.size());

    // -------------------------------------------------------------------------
    // 3. Derive sheet layout from meta.size
    // -------------------------------------------------------------------------
    if (j.contains("meta") && j["meta"].contains("size")) {
        const auto& sz = j["meta"]["size"];
        int sheetW = sz["w"].get<int>();
        int sheetH = sz["h"].get<int>();
        if (result.frameWidth > 0 && result.frameHeight > 0) {
            result.columns = sheetW / result.frameWidth;
            result.rows = sheetH / result.frameHeight;
        }
    }

    // -------------------------------------------------------------------------
    // 4-6. Parse frameTags, split names, synthesize left/right, handle durations
    // -------------------------------------------------------------------------
    if (j.contains("meta") && j["meta"].contains("frameTags")) {
        const auto& tags = j["meta"]["frameTags"];

        // 7. Pre-parse hit slices for lookup
        std::vector<int> hitFrameIndices; // absolute frame indices with hit markers
        if (j["meta"].contains("slices")) {
            for (const auto& slice : j["meta"]["slices"]) {
                if (slice.contains("name") && slice["name"].get<std::string>() == "hit") {
                    if (slice.contains("keys")) {
                        for (const auto& key : slice["keys"]) {
                            if (key.contains("frame")) {
                                hitFrameIndices.push_back(key["frame"].get<int>());
                            }
                        }
                    }
                }
            }
        }

        for (const auto& tag : tags) {
            std::string tagName = tag["name"].get<std::string>();
            int from = tag["from"].get<int>();
            int to = tag["to"].get<int>();
            int count = to - from + 1;

            std::string stateName, dirSuffix;
            splitTagName(tagName, stateName, dirSuffix);

            // Collect per-frame durations for this tag's range
            std::vector<int> durations;
            durations.reserve(count);
            for (int i = from; i <= to && i < static_cast<int>(frames.size()); ++i) {
                durations.push_back(frames[i].durationMs);
            }

            // Check if all durations are uniform
            bool uniform = true;
            if (!durations.empty()) {
                for (size_t i = 1; i < durations.size(); ++i) {
                    if (durations[i] != durations[0]) {
                        uniform = false;
                        break;
                    }
                }
            }

            float frameRate = 8.0f;
            if (uniform && !durations.empty() && durations[0] > 0) {
                frameRate = 1000.0f / static_cast<float>(durations[0]);
            }

            // Find hit frame within this tag's range
            int hitFrame = -1;
            for (int absIdx : hitFrameIndices) {
                if (absIdx >= from && absIdx <= to) {
                    hitFrame = absIdx - from;
                    break;
                }
            }

            bool loopDefault = inferLoop(stateName);

            // Build the ImportedState(s)
            auto makeState = [&](const std::string& dir, bool flip) -> ImportedState {
                ImportedState s;
                s.name = stateName;
                s.direction = dir;
                s.startFrame = from;
                s.frameCount = count;
                s.frameRate = frameRate;
                s.frameDurationsMs = uniform ? std::vector<int>{} : durations;
                s.hitFrame = hitFrame;
                s.loop = loopDefault;
                s.flipX = flip;
                return s;
            };

            // 5. Synthesize left/right from side
            if (dirSuffix == "side") {
                result.states.push_back(makeState("right", false));
                result.states.push_back(makeState("left", true));
            } else {
                result.states.push_back(makeState(dirSuffix, false));
            }
        }
    }

    return result;
}

AsepriteImportResult AsepriteImporter::mergeSiblings(
    const AsepriteImportResult& primary,
    const std::optional<AsepriteImportResult>& back,
    const std::optional<AsepriteImportResult>& side) {

    AsepriteImportResult merged;
    merged.frameWidth = primary.frameWidth;
    merged.frameHeight = primary.frameHeight;
    merged.columns = primary.columns;
    merged.rows = primary.rows;
    merged.totalFrames = primary.totalFrames;
    merged.sheetPath = primary.sheetPath;

    // Start with primary states
    for (const auto& s : primary.states) {
        merged.states.push_back(s);
    }

    // Append back states
    if (back.has_value()) {
        if (back->frameWidth != primary.frameWidth || back->frameHeight != primary.frameHeight) {
            merged.warnings.push_back("Back sibling frame dimensions differ: " +
                std::to_string(back->frameWidth) + "x" + std::to_string(back->frameHeight) +
                " vs primary " + std::to_string(primary.frameWidth) + "x" + std::to_string(primary.frameHeight));
        }
        if (back->totalFrames != primary.totalFrames) {
            merged.warnings.push_back("Back sibling frame count differs: " +
                std::to_string(back->totalFrames) + " vs primary " + std::to_string(primary.totalFrames));
        }
        for (const auto& s : back->states) {
            merged.states.push_back(s);
        }
    }

    // Append side states
    if (side.has_value()) {
        if (side->frameWidth != primary.frameWidth || side->frameHeight != primary.frameHeight) {
            merged.warnings.push_back("Side sibling frame dimensions differ: " +
                std::to_string(side->frameWidth) + "x" + std::to_string(side->frameHeight) +
                " vs primary " + std::to_string(primary.frameWidth) + "x" + std::to_string(primary.frameHeight));
        }
        if (side->totalFrames != primary.totalFrames) {
            merged.warnings.push_back("Side sibling frame count differs: " +
                std::to_string(side->totalFrames) + " vs primary " + std::to_string(primary.totalFrames));
        }
        for (const auto& s : side->states) {
            merged.states.push_back(s);
        }
    }

    return merged;
}

namespace {

// Try to parse a sibling JSON file into an AsepriteImportResult.
std::optional<AsepriteImportResult> parseSiblingFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return std::nullopt;

    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    return fate::AsepriteImporter::parseJson(j);
}

// Strip a directional suffix (_front, _back, _side) from a stem.
// Returns the base name and the suffix found, or empty suffix if none matched.
void stripDirectionSuffix(const std::string& stem, std::string& outBase, std::string& outSuffix) {
    static const std::string suffixes[] = {"_front", "_back", "_side"};
    for (const auto& suf : suffixes) {
        if (stem.size() > suf.size() &&
            stem.compare(stem.size() - suf.size(), suf.size(), suf) == 0) {
            outBase = stem.substr(0, stem.size() - suf.size());
            outSuffix = suf;
            return;
        }
    }
    outBase = stem;
    outSuffix = "";
}

} // anonymous namespace

std::optional<AsepriteImportResult> AsepriteImporter::parse(const std::string& jsonPath) {
    namespace fs = std::filesystem;

    // Parse the given file first
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_ERROR("AsepriteImporter", "Failed to open file: %s", jsonPath.c_str());
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("AsepriteImporter", "JSON parse error in %s: %s", jsonPath.c_str(), e.what());
        return std::nullopt;
    }

    auto primaryResult = parseJson(j);
    if (!primaryResult) return std::nullopt;

    // Derive companion .png path
    fs::path p(jsonPath);
    p.replace_extension(".png");
    primaryResult->sheetPath = p.string();

    // Check for directional suffix to decide if sibling discovery is needed
    std::string stem = p.stem().string();
    std::string baseName, suffix;
    stripDirectionSuffix(stem, baseName, suffix);

    if (suffix.empty()) {
        // No directional suffix -- single-file import
        return primaryResult;
    }

    // Sibling discovery: look for _front, _back, _side in the same directory
    fs::path dir = fs::path(jsonPath).parent_path();

    fs::path frontPath = dir / (baseName + "_front.json");
    fs::path backPath  = dir / (baseName + "_back.json");
    fs::path sidePath  = dir / (baseName + "_side.json");

    // Parse whichever siblings exist. The file that was explicitly passed is
    // always "primary", but we also need to ensure we parse front if a
    // different sibling was the entry point.
    std::optional<AsepriteImportResult> frontResult;
    std::optional<AsepriteImportResult> backResult;
    std::optional<AsepriteImportResult> sideResult;

    if (suffix == "_front") {
        frontResult = primaryResult;
    } else {
        frontResult = parseSiblingFile(frontPath);
    }

    if (suffix == "_back") {
        backResult = primaryResult;
    } else {
        backResult = parseSiblingFile(backPath);
    }

    if (suffix == "_side") {
        sideResult = primaryResult;
    } else {
        sideResult = parseSiblingFile(sidePath);
    }

    // Use front as primary if available, otherwise fall back to the originally parsed file
    const AsepriteImportResult& mergeBase = frontResult.has_value() ? *frontResult : *primaryResult;

    auto merged = mergeSiblings(mergeBase, backResult, sideResult);
    merged.sheetPath = primaryResult->sheetPath;

    // Warn about missing siblings
    if (!frontResult.has_value()) {
        merged.warnings.push_back("Missing sibling: " + frontPath.string());
    }
    if (!backResult.has_value()) {
        merged.warnings.push_back("Missing sibling: " + backPath.string());
    }
    if (!sideResult.has_value()) {
        merged.warnings.push_back("Missing sibling: " + sidePath.string());
    }

    return merged;
}

} // namespace fate
