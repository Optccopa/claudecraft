#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cc {

// Minimal read-only ZIP reader: enough to pull individual files out of a
// Minecraft resource pack. Decompresses stored (0) and deflate (8) entries —
// deflate reuses stb_image's raw-inflate, so there is no extra dependency.
// ZIP64 and encryption are unsupported (resource packs use neither).
class ZipArchive {
public:
    [[nodiscard]] static std::optional<ZipArchive> open(const std::filesystem::path& path);

    [[nodiscard]] bool contains(const std::string& name) const noexcept {
        return m_entries.contains(name);
    }
    // Decompressed bytes for an entry, or nullopt if missing or corrupt.
    [[nodiscard]] std::optional<std::vector<unsigned char>> read(const std::string& name) const;

private:
    struct Entry {
        std::uint16_t method;
        std::uint32_t compressedSize;
        std::uint32_t uncompressedSize;
        std::uint32_t localHeaderOffset;
    };

    explicit ZipArchive(std::vector<unsigned char> bytes) noexcept : m_bytes{std::move(bytes)} {}

    [[nodiscard]] bool parseCentralDirectory();

    std::vector<unsigned char> m_bytes;
    std::unordered_map<std::string, Entry> m_entries;
};

} // namespace cc
