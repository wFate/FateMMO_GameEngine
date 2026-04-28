#include "engine/game/skill_loadout.h"
#include "engine/net/protocol.h"
#include "engine/net/net_client.h"
#ifdef FATE_HAS_GAME
#include "game/shared/inventory.h"
#endif
#include <chrono>

namespace fate {

namespace {

// Returns current time as milliseconds since an arbitrary epoch (steady clock).
uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // anonymous namespace

SkillLoadout::SkillLoadout() = default;

const LoadoutSlot& SkillLoadout::slot(uint8_t flatIndex) const {
    static const LoadoutSlot empty{};
    if (flatIndex >= TOTAL_SLOTS) return empty;
    return slots_[flatIndex];
}

void SkillLoadout::setCurrentPage(uint8_t page) {
    if (page >= PAGE_COUNT) return;
    if (currentPage_ == page) return;
    currentPage_ = page;
    notifyChanged();
}

void SkillLoadout::onChanged(ChangeCallback cb) {
    observers_.push_back(std::move(cb));
}

void SkillLoadout::notifyChanged() {
    // Observers must not register/unregister or destroy *this from within their callback.
    for (const auto& cb : observers_) cb();
}

float SkillLoadout::remainingCooldown(uint8_t flatIndex) const {
    if (flatIndex >= TOTAL_SLOTS) return 0.0f;
    uint64_t now = nowMs();
    uint64_t end = cooldownEndsMs_[flatIndex];
    if (now >= end) return 0.0f;
    return static_cast<float>(end - now);
}

// Action codes for CmdAssignSlot wire protocol.
namespace {
    constexpr uint8_t ACTION_ASSIGN = 0;
    constexpr uint8_t ACTION_CLEAR  = 1;
    constexpr uint8_t ACTION_SWAP   = 2;
    constexpr uint8_t KIND_EMPTY    = 0;
    constexpr uint8_t KIND_SKILL    = 1;
    constexpr uint8_t KIND_ITEM     = 2;
}

void SkillLoadout::assignSkill(uint8_t flatIndex, const std::string& skillId) {
    if (flatIndex >= TOTAL_SLOTS) return;
    auto& s = slots_[flatIndex];
    s.kind = LoadoutSlotKind::Skill;
    s.skillId = skillId;
    s.instanceId.clear();
    if (netClient_) {
        netClient_->sendAssignSlot(ACTION_ASSIGN, KIND_SKILL, skillId, "", flatIndex, 0);
    }
    notifyChanged();
}

void SkillLoadout::assignItem(uint8_t flatIndex, const std::string& instanceId) {
    if (flatIndex >= TOTAL_SLOTS) return;
    auto& s = slots_[flatIndex];
    s.kind = LoadoutSlotKind::Item;
    s.skillId.clear();
    s.instanceId = instanceId;
    if (netClient_) {
        netClient_->sendAssignSlot(ACTION_ASSIGN, KIND_ITEM, "", instanceId, flatIndex, 0);
    }
    notifyChanged();
}

void SkillLoadout::clear(uint8_t flatIndex) {
    if (flatIndex >= TOTAL_SLOTS) return;
    slots_[flatIndex] = LoadoutSlot{};
    if (netClient_) {
        netClient_->sendAssignSlot(ACTION_CLEAR, KIND_EMPTY, "", "", flatIndex, 0);
    }
    notifyChanged();
}

void SkillLoadout::swap(uint8_t a, uint8_t b) {
    if (a >= TOTAL_SLOTS || b >= TOTAL_SLOTS) return;
    if (a == b) return;
    std::swap(slots_[a], slots_[b]);
    if (netClient_) {
        netClient_->sendAssignSlot(ACTION_SWAP, KIND_EMPTY, "", "", a, b);
    }
    notifyChanged();
}

// applyServerSync — adopt the server's authoritative loadout slot layout.
void SkillLoadout::applyServerSync(const SvSkillSyncMsg& msg) {
    if (msg.skillBar.size() != TOTAL_SLOTS) return;
    for (uint8_t i = 0; i < TOTAL_SLOTS; ++i) {
        const auto& wire = msg.skillBar[i];
        auto& s = slots_[i];
        switch (wire.kind) {
            case 1:
                s.kind = LoadoutSlotKind::Skill;
                s.skillId = wire.skillId;
                s.instanceId.clear();
                break;
            case 2:
                s.kind = LoadoutSlotKind::Item;
                s.skillId.clear();
                s.instanceId = wire.instanceId;
                break;
            case 0:
            default:
                s = LoadoutSlot{};
                break;
        }
    }
    notifyChanged();
}

int SkillLoadout::resolveItemQuantity(const std::string& instanceId) const {
#ifdef FATE_HAS_GAME
    if (!inventory_ || instanceId.empty()) return 0;
    int slot = inventory_->findByInstanceId(instanceId);
    if (slot >= 0) return inventory_->getSlot(slot).quantity;
    auto bagLoc = inventory_->findByInstanceIdInBags(instanceId);
    if (bagLoc.bagSlot >= 0 && bagLoc.subSlot >= 0) {
        return inventory_->getBagItem(bagLoc.bagSlot, bagLoc.subSlot).quantity;
    }
#else
    (void)instanceId;
#endif
    return 0;
}

bool SkillLoadout::resolveItemDef(const std::string& instanceId, ItemDef& out) const {
#ifdef FATE_HAS_GAME
    if (!inventory_ || instanceId.empty()) return false;
    int slot = inventory_->findByInstanceId(instanceId);
    if (slot >= 0) {
        const auto it = inventory_->getSlot(slot);
        out.id          = it.itemId;
        out.displayName = it.displayName;
        out.iconIndex   = -1;
        return true;
    }
    auto bagLoc = inventory_->findByInstanceIdInBags(instanceId);
    if (bagLoc.bagSlot >= 0 && bagLoc.subSlot >= 0) {
        const auto it = inventory_->getBagItem(bagLoc.bagSlot, bagLoc.subSlot);
        out.id          = it.itemId;
        out.displayName = it.displayName;
        out.iconIndex   = -1;
        return true;
    }
#else
    (void)instanceId;
    (void)out;
#endif
    return false;
}

void SkillLoadout::applyConsumableCooldown(const std::string& itemId, uint32_t cooldownMs) {
    if (!inventory_) return;
    const uint64_t end = nowMs() + static_cast<uint64_t>(cooldownMs);
    for (uint8_t i = 0; i < TOTAL_SLOTS; ++i) {
        if (slots_[i].kind != LoadoutSlotKind::Item) continue;
        ItemDef def;
        if (!resolveItemDef(slots_[i].instanceId, def)) continue;
        if (def.id == itemId) cooldownEndsMs_[i] = end;
    }
    notifyChanged();
}

} // namespace fate
