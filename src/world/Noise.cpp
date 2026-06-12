#include "world/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace cc {
namespace {

// Quintic fade gives C2 continuity at lattice points (Perlin's improved curve).
[[nodiscard]] float fade(float t) noexcept {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] float lerp(float a, float b, float t) noexcept {
    return a + t * (b - a);
}

// 8 gradient directions are enough for 2D and avoid a normalize.
[[nodiscard]] float grad(std::uint8_t hash, float x, float y) noexcept {
    switch (hash & 7u) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}

} // namespace

Noise::Noise(std::uint32_t seed) {
    std::array<std::uint8_t, 256> base{};
    std::iota(base.begin(), base.end(), std::uint8_t{0});
    std::mt19937 rng(seed);
    std::shuffle(base.begin(), base.end(), rng);
    for (std::size_t i = 0; i < 256; ++i) {
        m_perm[i] = base[i];
        m_perm[i + 256] = base[i];
    }
}

float Noise::perlin(float x, float y) const noexcept {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int xi = static_cast<int>(fx) & 255;
    const int yi = static_cast<int>(fy) & 255;
    const float dx = x - fx;
    const float dy = y - fy;

    const float u = fade(dx);
    const float v = fade(dy);

    const std::uint8_t aa = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi)] + yi)];
    const std::uint8_t ab = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi)] + yi + 1)];
    const std::uint8_t ba = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi) + 1] + yi)];
    const std::uint8_t bb = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi) + 1] + yi + 1)];

    const float x1 = lerp(grad(aa, dx, dy), grad(ba, dx - 1.0f, dy), u);
    const float x2 = lerp(grad(ab, dx, dy - 1.0f), grad(bb, dx - 1.0f, dy - 1.0f), u);
    // *1.41 rescales the theoretical 2D range (±sqrt(2)/2) to roughly ±1.
    return lerp(x1, x2, v) * 1.41f;
}

float Noise::fbm(float x, float y, int octaves, float lacunarity, float gain) const noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float range = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += perlin(x * frequency, y * frequency) * amplitude;
        range += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return sum / range;
}

} // namespace cc
