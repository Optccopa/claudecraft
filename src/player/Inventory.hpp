#pragma once

#include "world/Item.hpp"

#include <array>
#include <filesystem>
#include <span>

namespace cc {

// 36 stacked slots: 0..8 are the hotbar, the rest the grid rows. Persisted
// per world to player.dat (survival keeps its items across sessions).
class Inventory {
public:
    static constexpr int HotbarSize = 9;
    static constexpr int GridRows = 3;
    static constexpr int Size = HotbarSize * (GridRows + 1);

    [[nodiscard]] ItemStack& slot(int index) noexcept {
        return m_slots[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] const ItemStack& slot(int index) const noexcept {
        return m_slots[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] std::span<const ItemStack, HotbarSize> hotbar() const noexcept {
        return std::span<const ItemStack, HotbarSize>{m_slots.data(), HotbarSize};
    }

    // Fills matching stacks first, then empty slots (hotbar before grid).
    // Returns how many items did not fit.
    int add(BlockType type, int count) noexcept;
    // Removes one item from the slot; clears it when it hits zero.
    void consumeOne(int index) noexcept;
    void clear() noexcept { m_slots.fill(ItemStack{}); }

    void loadFrom(const std::filesystem::path& path);
    void saveTo(const std::filesystem::path& path) const;

private:
    std::array<ItemStack, Size> m_slots{};
};

} // namespace cc
