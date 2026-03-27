#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "engine/core/types.h"

namespace fate {

enum class Faction : uint8_t {
    None   = 0,
    Xyros  = 1,
    Fenor  = 2,
    Zethos = 3,
    Solis  = 4
};

struct FactionDefinition {
    Faction faction           = Faction::None;
    std::string displayName;
    Color color               = Color::white();
    std::string homeVillageId;
    std::string factionMerchantNPCId;
    std::string description;
};

class FactionRegistry {
public:
    FactionRegistry() = delete;

    static const FactionDefinition* get(Faction f) {
        auto idx = static_cast<uint8_t>(f);
        if (idx == 0 || idx > 4) return nullptr;
        return &s_factions[idx - 1];
    }

    static bool isSameFaction(Faction a, Faction b) {
        if (a == Faction::None || b == Faction::None) return false;
        return a == b;
    }

    /// Returns true if the given zone name is this faction's home village.
    /// Used for the PK exception: killing enemy faction in your village = no Red penalty.
    static bool isHomeVillage(Faction f, const std::string& zoneName) {
        const auto* def = get(f);
        if (!def || zoneName.empty()) return false;
        return def->homeVillageId == zoneName;
    }

private:
    static inline const std::array<FactionDefinition, 4> s_factions = {{
        { Faction::Xyros,  "Xyros",  {0.85f, 0.25f, 0.25f}, "zone_xyros_village",  "npc_merchant_xyros",
          "Born from volcanic highlands and forges carved into living rock. The Xyros believe strength is forged through trials -- their culture revolves around the flame as both creator and destroyer. Their village sits at the edge of a smoldering caldera. They speak of a \"First Ember\" -- a power buried deep below the world that they claim to be guardians of." },
        { Faction::Fenor,  "Fenor",  {0.25f, 0.55f, 0.85f}, "zone_fenor_village",  "npc_merchant_fenor",
          "A coastal people who built their civilization around tides, currents, and the deep. The Fenor value knowledge, patience, and the flow of things. Their village overlooks a vast sea, built into cliffs and tidal caves. They keep archives of the old world -- or claim to. Fenor scholars whisper about something sleeping beneath the ocean floor, something the cataclysm failed to destroy." },
        { Faction::Zethos, "Zethos", {0.25f, 0.80f, 0.40f}, "zone_zethos_village", "npc_merchant_zethos",
          "Deep in an ancient forest that predates any written record, the Zethos live in symbiosis with the land. They believe the world itself is alive and wounded -- that the cataclysm was not just destruction, but a scar the world still carries. Their shamans read signs in root patterns and animal migrations. They guard groves they refuse to let outsiders enter." },
        { Faction::Solis,  "Solis",  {0.95f, 0.75f, 0.20f}, "zone_solis_village",  "npc_merchant_solis",
          "The Solis built upward -- towers, spires, open plazas bathed in light. They are deeply immersed in study, architecture, and the pursuit of perfection. Their village sits on high plains where the sun never seems fully hidden. They believe the cataclysm was a consequence of chaos, and that only through discipline and knowledge can the world be restored." },
    }};
};

class FactionChatGarbler {
public:
    FactionChatGarbler() = delete;

    static std::string garble(const std::string& message) {
        if (message.empty()) return "";

        uint32_t seed = 0;
        for (char c : message) {
            seed = seed * 31 + static_cast<uint8_t>(c);
        }

        static constexpr std::string_view alphabet = "aeiouzxkwqjfpmt";
        std::string result;
        result.reserve(message.size());

        for (size_t i = 0; i < message.size(); ++i) {
            if (message[i] == ' ') {
                result += ' ';
            } else {
                seed = seed * 1103515245 + 12345 + static_cast<uint32_t>(i);
                result += alphabet[(seed >> 16) % alphabet.size()];
            }
        }
        return result;
    }
};

} // namespace fate
