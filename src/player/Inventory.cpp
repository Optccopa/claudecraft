#include "player/Inventory.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>

namespace cc {
namespace {

constexpr std::uint16_t kVersion = 1;

} // namespace

int Inventory::add(BlockType type, int count) noexcept {
    for (ItemStack& stack : m_slots) {
        if (count == 0) {
            break;
        }
        if (!stack.empty() && stack.type == type && stack.count < kMaxStackSize) {
            const int moved = std::min(count, kMaxStackSize - stack.count);
            stack.count += moved;
            count -= moved;
        }
    }
    for (ItemStack& stack : m_slots) {
        if (count == 0) {
            break;
        }
        if (stack.empty()) {
            const int moved = std::min(count, kMaxStackSize);
            stack = ItemStack{type, moved};
            count -= moved;
        }
    }
    return count;
}

void Inventory::consumeOne(int index) noexcept {
    ItemStack& stack = slot(index);
    if (--stack.count <= 0) {
        stack = ItemStack{};
    }
}

void Inventory::loadFrom(const std::filesystem::path& path) {
    clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return; // fresh world
    }
    std::uint16_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file || version != kVersion) {
        logError(std::format("ignoring incompatible player.dat ({})", path.string()));
        return;
    }
    for (ItemStack& stack : m_slots) {
        std::uint8_t type = 0;
        std::uint8_t count = 0;
        file.read(reinterpret_cast<char*>(&type), 1);
        file.read(reinterpret_cast<char*>(&count), 1);
        if (!file || type >= static_cast<std::uint8_t>(BlockType::Count) ||
            count > kMaxStackSize) {
            logError(std::format("corrupt player.dat ({})", path.string()));
            clear();
            return;
        }
        stack = ItemStack{static_cast<BlockType>(type), count};
    }
}

void Inventory::saveTo(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        logError(std::format("cannot write player.dat ({})", path.string()));
        return;
    }
    file.write(reinterpret_cast<const char*>(&kVersion), sizeof(kVersion));
    for (const ItemStack& stack : m_slots) {
        const auto type = static_cast<std::uint8_t>(stack.empty() ? BlockType::Air : stack.type);
        const auto count = static_cast<std::uint8_t>(stack.empty() ? 0 : stack.count);
        file.write(reinterpret_cast<const char*>(&type), 1);
        file.write(reinterpret_cast<const char*>(&count), 1);
    }
}

} // namespace cc
