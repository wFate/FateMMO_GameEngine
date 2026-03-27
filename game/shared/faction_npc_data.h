#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "game/shared/faction.h"

namespace fate {

enum class NPCRole : uint8_t {
    Shopkeeper,
    Banker,
    Teleporter,
    ArenaMaster,
    BattlefieldHerald,
    DungeonGuide,
    QuestGiver,
    Marketplace
};

struct FactionNPCDef {
    NPCRole role;
    std::string name;
    std::string greeting;
    std::string roleSubtitle;
};

struct LeaderboardNPCDef {
    std::string name;
    std::string greeting;
    std::string loreSnippet;
};

class FactionNPCData {
public:
    FactionNPCData() = delete;

    static const std::vector<FactionNPCDef>& getNPCs(Faction f) {
        auto idx = static_cast<uint8_t>(f);
        if (idx == 0 || idx > 4) {
            static const std::vector<FactionNPCDef> s_empty;
            return s_empty;
        }
        return s_factionNPCs[idx - 1];
    }

    static const LeaderboardNPCDef& getLeaderboardNPC(Faction f) {
        auto idx = static_cast<uint8_t>(f);
        if (idx == 0 || idx > 4) {
            static const LeaderboardNPCDef s_empty;
            return s_empty;
        }
        return s_leaderboardNPCs[idx - 1];
    }

private:
    // -- Xyros (Fire Kingdom) ------------------------------------------------
    static inline const std::vector<FactionNPCDef> s_xyrosNPCs = {
        { NPCRole::Shopkeeper,       "Cindrik",           "Forged in the deep fires. Take what you need.",                                "[Merchant]"    },
        { NPCRole::Banker,           "Vulkar",            "Your treasures won't melt here. I keep them sealed.",                          "[Banker]"      },
        { NPCRole::Teleporter,       "Pyrra",             "The embers remember every path. Where to?",                                    "[Teleporter]"  },
        { NPCRole::ArenaMaster,      "Blazeclaw Korr",    "Blood and fire await. Prove yourself.",                                        "[Arena]"       },
        { NPCRole::BattlefieldHerald,"Embercrier Nox",    "The front lines burn. Will you answer?",                                       "[Battlefield]" },
        { NPCRole::DungeonGuide,     "Ashveil Tygo",      "There are places below where even fire struggles to reach.",                    "[Dungeon]"     },
        { NPCRole::QuestGiver,       "Forge Elder Harsk", "You carry the look of someone untempered. The forge has work for you.",         "[Quest]"       },
        { NPCRole::Marketplace,      "Veylan",            "Veylan's Exchange -- where faction lines don't apply. Looking to trade?",       "[Market]"      },
    };

    // -- Fenor (Ocean Realm) -------------------------------------------------
    static inline const std::vector<FactionNPCDef> s_fenorNPCs = {
        { NPCRole::Shopkeeper,       "Mirelle",           "The tide brings all things in time. Browse freely.",                            "[Merchant]"    },
        { NPCRole::Banker,           "Coralith",          "Safe as the ocean floor. What do you need?",                                   "[Banker]"      },
        { NPCRole::Teleporter,       "Tidecaller Orin",   "The currents can carry you far. Choose your course.",                           "[Teleporter]"  },
        { NPCRole::ArenaMaster,      "Stormfang Reva",    "The storm tests all who enter. Ready?",                                        "[Arena]"       },
        { NPCRole::BattlefieldHerald,"Foghorn Tessa",     "The fog of war rolls in. Rally your allies.",                                   "[Battlefield]" },
        { NPCRole::DungeonGuide,     "Depthwatcher Lenn", "Depths no tide has touched. Tread carefully.",                                  "[Dungeon]"     },
        { NPCRole::QuestGiver,       "Tidescribe Yuna",   "New to these shores? The archives have need of willing hands.",                 "[Quest]"       },
        { NPCRole::Marketplace,      "Veylan",            "Veylan's Exchange -- where faction lines don't apply. Looking to trade?",       "[Market]"      },
    };

    // -- Zethos (Forest Tribe) -----------------------------------------------
    static inline const std::vector<FactionNPCDef> s_zethosNPCs = {
        { NPCRole::Shopkeeper,       "Thornwick",         "The grove provides. See what's grown today.",                                   "[Merchant]"    },
        { NPCRole::Banker,           "Mossden",           "Buried among roots, nothing is lost.",                                          "[Banker]"      },
        { NPCRole::Teleporter,       "Rootwalker Syva",   "The roots connect all things beneath. Step carefully.",                         "[Teleporter]"  },
        { NPCRole::ArenaMaster,      "Ironbark Dren",     "Strength without roots topples. Show me yours.",                                "[Arena]"       },
        { NPCRole::BattlefieldHerald,"Hollowcall Venn",   "The wilds call for defenders. Stand with us.",                                  "[Battlefield]" },
        { NPCRole::DungeonGuide,     "Briarshade Kael",   "Old hollows the forest grew over long ago. They're stirring.",                  "[Dungeon]"     },
        { NPCRole::QuestGiver,       "Grove Warden Elith","The grove warden speaks of you. There is something you must see.",              "[Quest]"       },
        { NPCRole::Marketplace,      "Veylan",            "Veylan's Exchange -- where faction lines don't apply. Looking to trade?",       "[Market]"      },
    };

    // -- Solis (Sun Empire) --------------------------------------------------
    static inline const std::vector<FactionNPCDef> s_solisNPCs = {
        { NPCRole::Shopkeeper,       "Solara",            "Only the essentials. Precision in all things.",                                 "[Merchant]"    },
        { NPCRole::Banker,           "Aurelis",           "Catalogued and accounted for. Always.",                                         "[Banker]"      },
        { NPCRole::Teleporter,       "Lumara",            "Every road mapped, every distance measured. Your destination?",                 "[Teleporter]"  },
        { NPCRole::ArenaMaster,      "Dawnblade Cael",    "Discipline decides every match. Enter when prepared.",                          "[Arena]"       },
        { NPCRole::BattlefieldHerald,"Clarion Mira",      "Order must be maintained on the field. Enlist.",                                "[Battlefield]" },
        { NPCRole::DungeonGuide,     "Brightward Sera",   "Our records speak of what lies within. Few match the accounts.",                "[Dungeon]"     },
        { NPCRole::QuestGiver,       "Sunscribe Dorin",   "Your name is not yet in our records. Let us change that.",                      "[Quest]"       },
        { NPCRole::Marketplace,      "Veylan",            "Veylan's Exchange -- where faction lines don't apply. Looking to trade?",       "[Market]"      },
    };

    // -- Faction NPC lookup array (indexed by Faction - 1) -------------------
    static inline const std::array<std::vector<FactionNPCDef>, 4> s_factionNPCs = {{
        s_xyrosNPCs,
        s_fenorNPCs,
        s_zethosNPCs,
        s_solisNPCs,
    }};

    // -- Leaderboard NPCs (one per faction, indexed by Faction - 1) ----------
    static inline const std::array<LeaderboardNPCDef, 4> s_leaderboardNPCs = {{
        { "Ashkeeper Renn",   "The stones remember every victory and every fall.",    "The Xyros carve names into volcanic glass. When the heat cracks the stone, they say it means the warrior has been surpassed."              },
        { "Tidekeeper Maren", "The currents carry names worth remembering.",          "The Fenor inscribe rankings on driftwood tablets. When the tide takes one, they say the sea has chosen to remember."                      },
        { "Mosskeeper Fael",  "The rings of the oldest trees mark the greatest among us.", "The Zethos read growth rings to mark the passage of champions. Each ring is a season, each season a trial survived."              },
        { "Sunkeeper Lyss",   "All deeds are recorded. The light hides nothing.",     "The Solis etch records into polished gold plates. They believe sunlight reveals the truth of every name written there."                   },
    }};
};

} // namespace fate
