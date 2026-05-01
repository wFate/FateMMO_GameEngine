#pragma once
#include <string>

namespace fate {

// LayoutClass — coarse device-shape bucket used to pick UI variant files.
//
// Lives under engine/render/ (alongside game_viewport.h) rather than under
// engine/ui/ so it is available to BOTH the proprietary game build (which
// owns engine/ui/) and the public demo build (which doesn't). Demo builds
// reach this header through the same engine/ tree, so app.cpp can include
// it unconditionally without an FATE_HAS_GAME guard.
//
// Cutoffs (landscape aspect = width / height):
//   tablet  : aspect <= 1.60   (iPads: 1.33 - 1.45)
//   compact : 1.60 < aspect <= 2.00   (iPhone SE / 720p / 1080p / 1440p / 4K @ 16:9 = 1.778)
//   base    : aspect > 2.00   (modern iPhones / Androids 2.16+, ultrawide 2.37)
//
// At load time, UIManager::loadScreen("foo.json") with class=tablet first tries
// "foo.tablet.json", then falls back to "foo.json" if missing. Same for compact.
// Base never adds a suffix — it loads the unmodified file.
//
// Authored content lives at the canonical (base) path. Variant files override
// only the screens that need different layouts on tablet / compact form factors.
enum class LayoutClass { Base, Compact, Tablet };

class LayoutClassRegistry {
public:
    // Classify a viewport by aspect (width / height). 0 or negative -> Base.
    static LayoutClass classify(float aspect) {
        if (aspect <= 0.0f) return LayoutClass::Base;
        if (aspect <= 1.60f) return LayoutClass::Tablet;
        if (aspect <= 2.00f) return LayoutClass::Compact;
        return LayoutClass::Base;
    }
    static LayoutClass classify(int width, int height) {
        if (width <= 0 || height <= 0) return LayoutClass::Base;
        return classify(static_cast<float>(width) / static_cast<float>(height));
    }

    static LayoutClass current() { return current_; }
    static bool set(LayoutClass cls) {
        if (current_ == cls) return false;
        current_ = cls;
        return true;  // caller should reload screens
    }

    // Variant suffix for filename mangling. Empty for Base (no suffix).
    static const char* suffix(LayoutClass cls) {
        switch (cls) {
            case LayoutClass::Tablet:  return "tablet";
            case LayoutClass::Compact: return "compact";
            case LayoutClass::Base:    return "";
        }
        return "";
    }
    static const char* suffix() { return suffix(current_); }

    static const char* name(LayoutClass cls) {
        switch (cls) {
            case LayoutClass::Tablet:  return "Tablet";
            case LayoutClass::Compact: return "Compact";
            case LayoutClass::Base:    return "Base";
        }
        return "Base";
    }
    static const char* name() { return name(current_); }

private:
    static inline LayoutClass current_ = LayoutClass::Base;
};

// Strip any recognized variant suffix from a path:
//   "foo.tablet.json"  -> "foo.json"
//   "foo.compact.json" -> "foo.json"
//   "foo.json"         -> "foo.json"   (unchanged)
//   "foo.tablet.tablet.json" -> "foo.tablet.json" (one strip per call; mangle is
//   idempotent so accidental double-suffix never makes it past mangleVariantPath
//   in the first place).
inline std::string canonicalBasePath(const std::string& path) {
    if (path.size() < 5) return path;
    if (path.compare(path.size() - 5, 5, ".json") != 0) return path;
    static const char* kSuffixes[] = { ".tablet", ".compact" };
    for (const char* sfx : kSuffixes) {
        size_t sfxLen = std::char_traits<char>::length(sfx);
        if (path.size() < 5 + sfxLen) continue;
        size_t suffixStart = path.size() - 5 - sfxLen;
        if (path.compare(suffixStart, sfxLen, sfx) == 0) {
            return path.substr(0, suffixStart) + ".json";
        }
    }
    return path;
}

// Compute "foo.tablet.json" from "foo.json" + class. If class is Base or the
// path doesn't end in .json, returns the input unchanged. Idempotent: if the
// path already ends in the target suffix, it is returned as-is so callers
// cannot accidentally produce "foo.tablet.tablet.json".
inline std::string mangleVariantPath(const std::string& basePath, LayoutClass cls) {
    const char* sfx = LayoutClassRegistry::suffix(cls);
    if (!sfx || !*sfx) return basePath;
    if (basePath.size() < 5) return basePath;
    if (basePath.compare(basePath.size() - 5, 5, ".json") != 0) return basePath;

    // Already mangled to the requested class? Return as-is.
    size_t sfxLen = std::char_traits<char>::length(sfx);
    if (basePath.size() >= 5 + 1 + sfxLen) {
        size_t expectStart = basePath.size() - 5 - sfxLen;
        if (basePath[expectStart - 1] == '.' &&
            basePath.compare(expectStart, sfxLen, sfx) == 0) {
            return basePath;
        }
    }

    // Strip any *other* variant suffix first so "foo.compact.json" + Tablet
    // becomes "foo.tablet.json", not "foo.compact.tablet.json".
    std::string canonical = canonicalBasePath(basePath);
    return canonical.substr(0, canonical.size() - 5) + "." + sfx + ".json";
}

} // namespace fate
