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

// Ken Perlin's improved-noise gradient set: 12 cube-edge directions
// (duplicated to 16 cases so the hash masks cheaply).
[[nodiscard]] float grad3(std::uint8_t hash, float x, float y, float z) noexcept {
    const std::uint8_t h = hash & 15u;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
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

float Noise::perlin3(float x, float y, float z) const noexcept {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float fz = std::floor(z);
    const int xi = static_cast<int>(fx) & 255;
    const int yi = static_cast<int>(fy) & 255;
    const int zi = static_cast<int>(fz) & 255;
    const float dx = x - fx;
    const float dy = y - fy;
    const float dz = z - fz;

    const float u = fade(dx);
    const float v = fade(dy);
    const float w = fade(dz);

    const auto perm = [this](int i) noexcept { return m_perm[static_cast<std::size_t>(i)]; };
    const int a = perm(xi) + yi;
    const int aa = perm(a) + zi;
    const int ab = perm(a + 1) + zi;
    const int b = perm(xi + 1) + yi;
    const int ba = perm(b) + zi;
    const int bb = perm(b + 1) + zi;

    const float x00 = lerp(grad3(perm(aa), dx, dy, dz), grad3(perm(ba), dx - 1, dy, dz), u);
    const float x10 = lerp(grad3(perm(ab), dx, dy - 1, dz), grad3(perm(bb), dx - 1, dy - 1, dz), u);
    const float x01 =
        lerp(grad3(perm(aa + 1), dx, dy, dz - 1), grad3(perm(ba + 1), dx - 1, dy, dz - 1), u);
    const float x11 = lerp(grad3(perm(ab + 1), dx, dy - 1, dz - 1),
                           grad3(perm(bb + 1), dx - 1, dy - 1, dz - 1), u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
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

float Noise::fbm3(float x, float y, float z, int octaves, float lacunarity,
                  float gain) const noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float range = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += perlin3(x * frequency, y * frequency, z * frequency) * amplitude;
        range += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return sum / range;
}

} // namespace cc
