#include "engine/vfx/skill_vfx_player.h"
#include "engine/render/texture.h"
#include "engine/core/logger.h"
#include <filesystem>
#include <algorithm>

namespace fate {

// ============================================================================
// ActiveEffect helpers
// ============================================================================

const VFXPhaseDef* ActiveEffect::currentPhaseDef() const {
    if (!def) return nullptr;
    switch (phase) {
        case VFXPhase::Cast:       return &def->cast;
        case VFXPhase::Projectile: return &def->projectile;
        case VFXPhase::Impact:     return &def->impact;
        case VFXPhase::Area:       return &def->area;
        default:                   return nullptr;
    }
}

Vec2 ActiveEffect::anchorPos() const {
    return currentPos;
}

// ============================================================================
// SkillVFXPlayer singleton
// ============================================================================

SkillVFXPlayer& SkillVFXPlayer::instance() {
    static SkillVFXPlayer s_instance;
    return s_instance;
}

void SkillVFXPlayer::loadDefinitions(const std::string& directory) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
        LOG_WARN("VFX", "VFX directory not found: %s", directory.c_str());
        return;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        SkillVFXDef def;
        if (loadSkillVFXDef(entry.path().string(), def) && !def.id.empty()) {
            defs_[def.id] = std::move(def);
            ++count;
        }
    }

    LOG_INFO("VFX", "Loaded %d VFX definitions from %s", count, directory.c_str());
}

void SkillVFXPlayer::registerDef(const SkillVFXDef& def) {
    defs_[def.id] = def;
}

const SkillVFXDef* SkillVFXPlayer::getDef(const std::string& id) const {
    auto it = defs_.find(id);
    if (it == defs_.end()) return nullptr;
    return &it->second;
}

void SkillVFXPlayer::play(const std::string& vfxId, Vec2 casterPos, Vec2 targetPos, bool hasTarget) {
    const SkillVFXDef* def = getDef(vfxId);
    if (!def) return;

    // Find first enabled phase
    VFXPhase firstPhase = VFXPhase::Done;
    if (def->cast.enabled)       firstPhase = VFXPhase::Cast;
    else if (def->projectile.enabled) firstPhase = VFXPhase::Projectile;
    else if (def->impact.enabled)     firstPhase = VFXPhase::Impact;
    else if (def->area.enabled)       firstPhase = VFXPhase::Area;

    if (firstPhase == VFXPhase::Done) return; // no phases enabled

    ActiveEffect fx;
    fx.def = def;
    fx.casterPos = casterPos;
    fx.targetPos = targetPos;
    fx.hasTarget = hasTarget;
    fx.phase = firstPhase;

    startPhase(fx);

    // Evict oldest if at capacity
    if (effects_.size() >= MAX_EFFECTS) {
        effects_.erase(effects_.begin());
    }

    effects_.push_back(std::move(fx));
}

void SkillVFXPlayer::startPhase(ActiveEffect& fx) {
    fx.currentFrame = 0;
    fx.frameElapsed = 0.0f;

    // Set position based on phase
    switch (fx.phase) {
        case VFXPhase::Cast:
            fx.currentPos = fx.casterPos;
            break;
        case VFXPhase::Projectile:
            fx.currentPos = fx.casterPos;
            break;
        case VFXPhase::Impact:
            fx.currentPos = fx.hasTarget ? fx.targetPos : fx.casterPos;
            break;
        case VFXPhase::Area:
            fx.currentPos = fx.hasTarget ? fx.targetPos : fx.casterPos;
            break;
        default:
            break;
    }

    const VFXPhaseDef* phaseDef = fx.currentPhaseDef();
    if (!phaseDef) return;

    // Set area timer
    if (fx.phase == VFXPhase::Area) {
        fx.areaTimeLeft = phaseDef->duration;
    }

    // Destroy previous particles, set up new ones if needed
    fx.particles.reset();
    if (phaseDef->particles.enabled) {
        fx.particles = std::make_unique<ParticleEmitter>();
        EmitterConfig ec = toEmitterConfig(phaseDef->particles, fx.currentPos);
        fx.particles->init(ec);
        fx.particles->burst(fx.currentPos, phaseDef->particles.count);
    }
}

void SkillVFXPlayer::advancePhase(ActiveEffect& fx) {
    fx.particles.reset();

    // Find next enabled phase after current
    VFXPhase next = VFXPhase::Done;
    int currentOrd = static_cast<int>(fx.phase);

    // Phase order: Cast=0, Projectile=1, Impact=2, Area=3
    const VFXPhaseDef* phases[] = { &fx.def->cast, &fx.def->projectile,
                                     &fx.def->impact, &fx.def->area };
    const VFXPhase phaseEnums[] = { VFXPhase::Cast, VFXPhase::Projectile,
                                     VFXPhase::Impact, VFXPhase::Area };

    for (int i = currentOrd + 1; i < 4; ++i) {
        if (phases[i]->enabled) {
            next = phaseEnums[i];
            break;
        }
    }

    fx.phase = next;
    if (next != VFXPhase::Done) {
        startPhase(fx);
    }
}

void SkillVFXPlayer::update(float dt) {
    for (auto& fx : effects_) {
        if (fx.phase == VFXPhase::Done) continue;

        const VFXPhaseDef* phaseDef = fx.currentPhaseDef();
        if (!phaseDef) {
            fx.phase = VFXPhase::Done;
            continue;
        }

        // Advance frame timing
        fx.frameElapsed += dt;
        float frameDuration = (phaseDef->frameRate > 0.0f) ? (1.0f / phaseDef->frameRate) : 1.0f;
        while (fx.frameElapsed >= frameDuration) {
            fx.currentFrame++;
            fx.frameElapsed -= frameDuration;
        }

        // Phase-specific logic
        switch (fx.phase) {
            case VFXPhase::Cast:
            case VFXPhase::Impact: {
                if (fx.currentFrame >= phaseDef->frameCount) {
                    advancePhase(fx);
                }
                break;
            }
            case VFXPhase::Projectile: {
                // Wrap frame for looping projectile anim
                if (phaseDef->frameCount > 0 && fx.currentFrame >= phaseDef->frameCount) {
                    fx.currentFrame = fx.currentFrame % phaseDef->frameCount;
                }

                // Move toward target
                Vec2 dir = fx.targetPos - fx.currentPos;
                float dist = dir.length();
                float step = phaseDef->speed * dt;
                if (dist < 4.0f || step >= dist) {
                    fx.currentPos = fx.targetPos;
                    advancePhase(fx);
                } else {
                    dir = dir * (1.0f / dist);
                    fx.currentPos = fx.currentPos + dir * step;
                }
                break;
            }
            case VFXPhase::Area: {
                // Loop animation if needed
                if (phaseDef->frameCount > 0 && fx.currentFrame >= phaseDef->frameCount && phaseDef->looping) {
                    fx.currentFrame = fx.currentFrame % phaseDef->frameCount;
                }

                fx.areaTimeLeft -= dt;
                if (fx.areaTimeLeft <= 0.0f) {
                    advancePhase(fx);
                }
                break;
            }
            default:
                break;
        }

        // Update particles
        if (fx.particles && fx.phase != VFXPhase::Done) {
            fx.particles->update(dt, fx.currentPos);
        }
    }

    // Remove completed effects
    effects_.erase(
        std::remove_if(effects_.begin(), effects_.end(),
            [](const ActiveEffect& fx) { return fx.phase == VFXPhase::Done; }),
        effects_.end());
}

void SkillVFXPlayer::render(SpriteBatch& batch, SDFText& /*sdf*/) {
    for (const auto& fx : effects_) {
        if (fx.phase == VFXPhase::Done) continue;

        const VFXPhaseDef* phaseDef = fx.currentPhaseDef();
        if (!phaseDef || phaseDef->spriteSheet.empty()) goto render_particles;

        {
            auto tex = TextureCache::instance().get(phaseDef->spriteSheet);
            if (!tex) {
                tex = TextureCache::instance().load(phaseDef->spriteSheet);
            }
            if (tex && tex->width() > 0 && tex->height() > 0) {
                float texW = static_cast<float>(tex->width());
                float texH = static_cast<float>(tex->height());

                float srcX = fx.currentFrame * phaseDef->frameSize.x / texW;
                float srcW = phaseDef->frameSize.x / texW;
                float srcH = phaseDef->frameSize.y / texH;

                SpriteDrawParams p;
                p.position = fx.anchorPos() + phaseDef->offset;
                p.size = phaseDef->frameSize;
                p.sourceRect = Rect{srcX, 0.0f, srcW, srcH};
                p.depth = 500.0f;

                batch.draw(tex, p);
            }
        }

    render_particles:
        // Render particle vertices as colored rects
        if (fx.particles && fx.particles->activeCount() > 0) {
            const auto& verts = fx.particles->vertices();
            // Particles are quads (4 verts each), render as small rects
            for (size_t i = 0; i + 3 < verts.size(); i += 4) {
                const auto& v0 = verts[i];
                const auto& v2 = verts[i + 2];
                Vec2 center = {(v0.x + v2.x) * 0.5f, (v0.y + v2.y) * 0.5f};
                float w = v2.x - v0.x;
                float h = v2.y - v0.y;
                if (w < 0.0f) w = -w;
                if (h < 0.0f) h = -h;
                Color c = {v0.r, v0.g, v0.b, v0.a};
                batch.drawRect(center, {w, h}, c, 501.0f);
            }
        }
    }
}

void SkillVFXPlayer::clear() {
    effects_.clear();
}

size_t SkillVFXPlayer::activeCount() const {
    return effects_.size();
}

EmitterConfig SkillVFXPlayer::toEmitterConfig(const VFXParticleConfig& pc, const Vec2& /*pos*/) const {
    EmitterConfig ec;
    ec.burstCount = pc.count;
    ec.spawnRate = 0.0f; // burst-only, no continuous spawn
    ec.velocityMin = {-pc.speed, -pc.speed};
    ec.velocityMax = { pc.speed,  pc.speed};
    ec.lifetimeMin = pc.lifetime;
    ec.lifetimeMax = pc.lifetime;
    ec.sizeMin = pc.sizeStart;
    ec.sizeMax = pc.sizeStart;
    ec.colorStart = pc.colorStart;
    ec.colorEnd = pc.colorEnd;
    ec.gravity = {0.0f, pc.gravity};
    ec.worldSpace = true;
    return ec;
}

} // namespace fate
