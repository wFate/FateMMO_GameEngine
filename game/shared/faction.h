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

private:
    static inline const std::array<FactionDefinition, 4> s_factions = {{
        { Faction::Xyros,  "Xyros",  {0.85f, 0.25f, 0.25f}, "zone_xyros_village",  "npc_merchant_xyros"  },
        { Faction::Fenor,  "Fenor",  {0.25f, 0.55f, 0.85f}, "zone_fenor_village",  "npc_merchant_fenor"  },
        { Faction::Zethos, "Zethos", {0.25f, 0.80f, 0.40f}, "zone_zethos_village", "npc_merchant_zethos" },
        { Faction::Solis,  "Solis",  {0.95f, 0.75f, 0.20f}, "zone_solis_village",  "npc_merchant_solis"  },
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
