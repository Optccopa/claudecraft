#pragma once

#include "world/Block.hpp"

#include <cstdint>

namespace cc {

enum class GameMode : std::uint8_t { Creative, Survival };

// Items are block types; an empty stack is Air with count 0.
struct ItemStack {
    BlockType type = BlockType::Air;
    int count = 0;

    [[nodiscard]] bool empty() const noexcept { return count == 0 || type == BlockType::Air; }
};

inline constexpr int kMaxStackSize = 64;

} // namespace cc
