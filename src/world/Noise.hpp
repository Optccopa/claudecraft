#pragma once

#include <array>
#include <cstdint>

namespace cc {

// Classic 2D Perlin noise with a seed-shuffled permutation table.
// Deterministic for a given seed; safe to call from any thread (const).
class Noise {
public:
    explicit Noise(std::uint32_t seed);

    // Single octave in roughly [-1, 1].
    [[nodiscard]] float perlin(float x, float y) const noexcept;
    [[nodiscard]] float perlin3(float x, float y, float z) const noexcept;

    // Fractal Brownian motion; result stays roughly in [-1, 1].
    [[nodiscard]] float fbm(float x, float y, int octaves, float lacunarity,
                            float gain) const noexcept;
    [[nodiscard]] float fbm3(float x, float y, float z, int octaves, float lacunarity,
                             float gain) const noexcept;

private:
    std::array<std::uint8_t, 512> m_perm{};
};

} // namespace cc
