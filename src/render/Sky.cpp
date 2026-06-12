#include "render/Sky.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cc {
namespace {

constexpr glm::vec3 kDaySky{0.55f, 0.74f, 0.95f};
constexpr glm::vec3 kNightSky{0.015f, 0.025f, 0.06f};
constexpr glm::vec3 kHorizonGlow{0.93f, 0.49f, 0.26f};

// Moonlight floor: full night still reads silhouettes without washing out
// block light.
constexpr float kNightSkyLight = 0.12f;

[[nodiscard]] float smoothstep01(float edge0, float edge1, float x) noexcept {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

SkyState skyStateAt(double timeOfDay) noexcept {
    const float angle =
        static_cast<float>(timeOfDay * 2.0 * std::numbers::pi_v<double>);
    const float elevation = std::sin(angle);

    // The dawn ramp starts slightly below the horizon so light precedes the
    // sun, and saturates well before noon so midday is stable full bright.
    const float day = smoothstep01(-0.06f, 0.25f, elevation);

    // Horizon glow peaks when the sun crosses the horizon (dawn and dusk).
    float glow = std::clamp(1.0f - std::abs(elevation) * 3.5f, 0.0f, 1.0f);
    glow *= glow;

    const glm::vec3 base = kNightSky + (kDaySky - kNightSky) * day;

    return SkyState{
        glm::normalize(glm::vec3{std::cos(angle), elevation, 0.30f}),
        base + (kHorizonGlow - base) * (glow * 0.55f),
        kNightSkyLight + (1.0f - kNightSkyLight) * day,
    };
}

} // namespace cc
