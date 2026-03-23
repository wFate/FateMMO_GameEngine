#pragma once
#include "engine/vfx/skill_vfx_def.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include "engine/particle/particle_emitter.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace fate {

enum class VFXPhase : uint8_t { Cast, Projectile, Impact, Area, Done };

struct ActiveEffect {
    const SkillVFXDef* def = nullptr;
    VFXPhase phase = VFXPhase::Cast;
    Vec2 casterPos, targetPos, currentPos;
    bool hasTarget = true;
    int currentFrame = 0;
    float frameElapsed = 0.0f;
    float areaTimeLeft = 0.0f;
    std::unique_ptr<ParticleEmitter> particles;

    const VFXPhaseDef* currentPhaseDef() const;
    Vec2 anchorPos() const;
};

class SkillVFXPlayer {
public:
    static SkillVFXPlayer& instance();

    void loadDefinitions(const std::string& directory = "assets/vfx/");

    // Register a definition directly (for testing without files)
    void registerDef(const SkillVFXDef& def);
    const SkillVFXDef* getDef(const std::string& id) const;

    void play(const std::string& vfxId, Vec2 casterPos, Vec2 targetPos, bool hasTarget = true);
    void update(float dt);
    void render(SpriteBatch& batch, SDFText& sdf);
    void clear();
    size_t activeCount() const;

    const std::vector<ActiveEffect>& effects() const { return effects_; }

    static constexpr size_t MAX_EFFECTS = 32;

private:
    SkillVFXPlayer() = default;
    std::unordered_map<std::string, SkillVFXDef> defs_;
    std::vector<ActiveEffect> effects_;

    void advancePhase(ActiveEffect& fx);
    void startPhase(ActiveEffect& fx);
    EmitterConfig toEmitterConfig(const VFXParticleConfig& pc, const Vec2& pos) const;
};

} // namespace fate
