#include "world/Mobs.hpp"

#include "core/Log.hpp"
#include "world/World.hpp"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>

namespace cc {
namespace {

constexpr float kGravity = 24.0f;
constexpr std::size_t kMaxMobs = 48;
constexpr float kHurtFlashTime = 0.25f;

// xorshift32 — cheap deterministic stream so wander/spawn stay reproducible
// from the caller's seed without dragging in <random> per call.
[[nodiscard]] std::uint32_t nextRng(std::uint32_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

[[nodiscard]] float randUnit(std::uint32_t& s) noexcept {
    return static_cast<float>(nextRng(s) & 0xFFFFu) / 65535.0f;
}

// True if the mob's AABB (feet centre at pos) overlaps any solid cell. A small
// inset keeps it from sticking to walls it merely grazes.
[[nodiscard]] bool collides(const World& world, const glm::vec3& pos, float halfWidth,
                            float height) noexcept {
    constexpr float e = 0.001f;
    const int x0 = static_cast<int>(std::floor(pos.x - halfWidth + e));
    const int x1 = static_cast<int>(std::floor(pos.x + halfWidth - e));
    const int z0 = static_cast<int>(std::floor(pos.z - halfWidth + e));
    const int z1 = static_cast<int>(std::floor(pos.z + halfWidth - e));
    const int y0 = static_cast<int>(std::floor(pos.y + e));
    const int y1 = static_cast<int>(std::floor(pos.y + height - e));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                if (blockInfo(world.blockAt({x, y, z})).solid) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Ray vs the mob's world AABB (slab method). Returns the near hit distance, or
// a negative value when the ray misses.
[[nodiscard]] float rayAabb(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& mn,
                            const glm::vec3& mx) noexcept {
    float tmin = 0.0f;
    float tmax = 1e9f;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = dir[axis];
        if (std::abs(d) < 1e-6f) {
            if (o < mn[axis] || o > mx[axis]) {
                return -1.0f;
            }
            continue;
        }
        float t1 = (mn[axis] - o) / d;
        float t2 = (mx[axis] - o) / d;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return -1.0f;
        }
    }
    return tmin;
}

} // namespace

void Mobs::update(float dt, const World& world, std::uint32_t& rng) {
    for (MobEntity& m : m_mobs) {
        const MobInfo& info = mobInfo(m.type);

        m.wanderTimer -= dt;
        if (m.wanderTimer <= 0.0f) {
            const std::uint32_t r = nextRng(rng);
            if ((r & 3u) == 0u) {
                m.moving = false;
                m.wanderTimer = 2.0f + randUnit(rng) * 3.0f;
            } else {
                m.moving = true;
                m.yaw = static_cast<float>(r % 360u);
                m.wanderTimer = 2.5f + randUnit(rng) * 3.5f;
            }
        }

        glm::vec3 vel = m.velocity;
        if (m.moving) {
            const float rad = glm::radians(m.yaw);
            vel.x = std::sin(rad) * info.walkSpeed;
            vel.z = std::cos(rad) * info.walkSpeed;
        } else {
            vel.x = 0.0f;
            vel.z = 0.0f;
        }
        vel.y -= kGravity * dt;

        glm::vec3 next = m.position;

        // Horizontal first, with a one-block auto step so herds climb gentle
        // terrain instead of jamming against it.
        const auto stepAxis = [&](float& coord, float delta, float& v) {
            const float prev = coord;
            coord += delta;
            if (collides(world, next, info.halfWidth, info.height)) {
                glm::vec3 stepped = next;
                stepped.y += 1.0f;
                if (m.onGround && !collides(world, stepped, info.halfWidth, info.height)) {
                    next = stepped;
                } else {
                    coord = prev;
                    v = 0.0f;
                }
            }
        };
        stepAxis(next.x, vel.x * dt, vel.x);
        stepAxis(next.z, vel.z * dt, vel.z);

        const float preY = next.y;
        next.y += vel.y * dt;
        m.onGround = false;
        if (collides(world, next, info.halfWidth, info.height)) {
            if (vel.y < 0.0f) {
                m.onGround = true;
            }
            next.y = preY;
            vel.y = 0.0f;
        }

        m.position = next;
        m.velocity = vel;
        m.hurtFlash = std::max(m.hurtFlash - dt, 0.0f);
    }

    // Drop through the world floor is the despawn guard; everything else is
    // culled by distance from Application.
    std::erase_if(m_mobs, [](const MobEntity& m) { return m.position.y < -8.0f; });
}

int Mobs::spawnHerd(const World& world, const glm::vec3& center, MobType type, int count,
                    std::uint32_t& rng) {
    int spawned = 0;
    const MobInfo& info = mobInfo(type);
    for (int i = 0; i < count && m_mobs.size() < kMaxMobs; ++i) {
        const float ox = (randUnit(rng) - 0.5f) * 8.0f;
        const float oz = (randUnit(rng) - 0.5f) * 8.0f;
        const int cx = static_cast<int>(std::floor(center.x + ox));
        const int cz = static_cast<int>(std::floor(center.z + oz));

        // Drop onto the first grass block with two cells of clearance — the
        // vanilla passive-spawn requirement.
        const int top = static_cast<int>(std::floor(center.y)) + 6;
        for (int y = top; y > top - 16; --y) {
            if (world.blockAt({cx, y, cz}) != BlockType::Grass) {
                continue;
            }
            if (blockInfo(world.blockAt({cx, y + 1, cz})).solid ||
                blockInfo(world.blockAt({cx, y + 2, cz})).solid) {
                break;
            }
            const glm::vec3 pos{static_cast<float>(cx) + 0.5f, static_cast<float>(y + 1),
                                static_cast<float>(cz) + 0.5f};
            m_mobs.push_back(MobEntity{type, pos, glm::vec3{0.0f}, randUnit(rng) * 360.0f,
                                       info.health});
            ++spawned;
            break;
        }
    }
    return spawned;
}

bool Mobs::attack(const glm::vec3& origin, const glm::vec3& dir, float reach, float damage,
                  std::uint32_t& rng, std::vector<Loot>& loot) {
    std::size_t best = m_mobs.size();
    float bestT = reach;
    for (std::size_t i = 0; i < m_mobs.size(); ++i) {
        const MobEntity& m = m_mobs[i];
        const MobInfo& info = mobInfo(m.type);
        const glm::vec3 mn{m.position.x - info.halfWidth, m.position.y,
                           m.position.z - info.halfWidth};
        const glm::vec3 mx{m.position.x + info.halfWidth, m.position.y + info.height,
                           m.position.z + info.halfWidth};
        const float t = rayAabb(origin, dir, mn, mx);
        if (t >= 0.0f && t <= bestT) {
            bestT = t;
            best = i;
        }
    }
    if (best == m_mobs.size()) {
        return false;
    }

    MobEntity& hit = m_mobs[best];
    hit.health -= damage;
    hit.hurtFlash = kHurtFlashTime;
    // Knock the mob back along the horizontal look direction so hits read.
    const glm::vec3 flat{dir.x, 0.0f, dir.z};
    if (const float len = glm::length(flat); len > 1e-4f) {
        hit.velocity += flat * (4.0f / len);
    }
    hit.velocity.y = std::max(hit.velocity.y, 3.0f);
    if (hit.health > 0.0f) {
        return true;
    }

    const MobInfo& info = mobInfo(hit.type);
    const glm::vec3 dropPos = hit.position + glm::vec3{0.0f, info.height * 0.4f, 0.0f};
    for (const MobDrop& d : info.drops) {
        if (d.type == BlockType::Air) {
            continue;
        }
        const int span = d.maxCount - d.minCount + 1;
        const int n = d.minCount + static_cast<int>(nextRng(rng) % static_cast<std::uint32_t>(span));
        if (n > 0) {
            loot.push_back(Loot{d.type, n, dropPos});
        }
    }
    m_mobs.erase(m_mobs.begin() + static_cast<std::ptrdiff_t>(best));
    return true;
}

void Mobs::cullDistant(const glm::vec3& center, float radius) {
    const float r2 = radius * radius;
    std::erase_if(m_mobs, [&](const MobEntity& m) {
        const float dx = m.position.x - center.x;
        const float dz = m.position.z - center.z;
        return dx * dx + dz * dz > r2;
    });
}

namespace {

constexpr std::uint32_t kMobMagic = 0x424F4D43u; // "CMOB"
constexpr std::uint32_t kMobVersion = 1;

#pragma pack(push, 1)
struct MobRecord {
    std::uint8_t type;
    float x, y, z;
    float yaw;
    float health;
};
#pragma pack(pop)

} // namespace

void Mobs::saveTo(const std::filesystem::path& path) const {
    if (m_mobs.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec); // stale herd shouldn't linger
        return;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        logError(std::format("cannot write mobs to '{}'", path.string()));
        return;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(m_mobs.size());
    file.write(reinterpret_cast<const char*>(&kMobMagic), sizeof(kMobMagic));
    file.write(reinterpret_cast<const char*>(&kMobVersion), sizeof(kMobVersion));
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const MobEntity& m : m_mobs) {
        const MobRecord rec{static_cast<std::uint8_t>(m.type), m.position.x, m.position.y,
                            m.position.z, m.yaw, m.health};
        file.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }
}

void Mobs::loadFrom(const std::filesystem::path& path) {
    m_mobs.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!file || magic != kMobMagic || version != kMobVersion) {
        logError(std::format("invalid mob file '{}'", path.string()));
        return;
    }
    m_mobs.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        MobRecord rec{};
        file.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!file || rec.type >= static_cast<std::uint8_t>(MobType::Count) || rec.health <= 0.0f) {
            break; // truncated or corrupt — keep whatever loaded cleanly
        }
        m_mobs.push_back(MobEntity{static_cast<MobType>(rec.type),
                                   glm::vec3{rec.x, rec.y, rec.z}, glm::vec3{0.0f}, rec.yaw,
                                   rec.health});
    }
}

} // namespace cc
