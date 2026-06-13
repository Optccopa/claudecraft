#include "core/ZipArchive.hpp"

#include "core/Log.hpp"

#include <stb_image.h> // raw-inflate (stbi_zlib_decode_noheader_buffer)

#include <format>
#include <fstream>

namespace cc {
namespace {

constexpr std::uint32_t kEocdSignature = 0x06054b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::size_t kEocdMinSize = 22;
constexpr std::size_t kCentralEntryMinSize = 46;

[[nodiscard]] std::uint16_t readU16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

[[nodiscard]] std::uint32_t readU32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

std::optional<ZipArchive> ZipArchive::open(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < static_cast<std::streamoff>(kEocdMinSize)) {
        return std::nullopt;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return std::nullopt;
    }

    ZipArchive archive{std::move(bytes)};
    if (!archive.parseCentralDirectory()) {
        logError(std::format("'{}' is not a readable zip", path.string()));
        return std::nullopt;
    }
    return archive;
}

bool ZipArchive::parseCentralDirectory() {
    // The end-of-central-directory record sits at the tail, possibly behind a
    // comment, so scan backwards for its signature.
    const std::size_t n = m_bytes.size();
    std::size_t eocd = n;
    for (std::size_t i = n - kEocdMinSize + 1; i-- > 0;) {
        if (readU32(&m_bytes[i]) == kEocdSignature) {
            eocd = i;
            break;
        }
    }
    if (eocd == n) {
        return false;
    }

    const std::uint16_t entryCount = readU16(&m_bytes[eocd + 10]);
    const std::uint32_t centralOffset = readU32(&m_bytes[eocd + 16]);

    std::size_t cursor = centralOffset;
    m_entries.reserve(entryCount);
    for (std::uint16_t i = 0; i < entryCount; ++i) {
        if (cursor + kCentralEntryMinSize > n || readU32(&m_bytes[cursor]) != kCentralSignature) {
            return false;
        }
        const std::uint16_t method = readU16(&m_bytes[cursor + 10]);
        const std::uint32_t compressedSize = readU32(&m_bytes[cursor + 20]);
        const std::uint32_t uncompressedSize = readU32(&m_bytes[cursor + 24]);
        const std::uint16_t nameLen = readU16(&m_bytes[cursor + 28]);
        const std::uint16_t extraLen = readU16(&m_bytes[cursor + 30]);
        const std::uint16_t commentLen = readU16(&m_bytes[cursor + 32]);
        const std::uint32_t localOffset = readU32(&m_bytes[cursor + 42]);

        if (cursor + kCentralEntryMinSize + nameLen > n) {
            return false;
        }
        std::string name(reinterpret_cast<const char*>(&m_bytes[cursor + kCentralEntryMinSize]),
                         nameLen);
        m_entries.emplace(std::move(name),
                          Entry{method, compressedSize, uncompressedSize, localOffset});
        cursor += kCentralEntryMinSize + nameLen + extraLen + commentLen;
    }
    return true;
}

std::optional<std::vector<unsigned char>> ZipArchive::read(const std::string& name) const {
    const auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    const Entry& e = it->second;

    // The central directory's local-header offset points at a local header
    // whose own name/extra lengths give the true data offset (they can differ
    // from the central copy).
    constexpr std::size_t kLocalHeaderMinSize = 30;
    if (e.localHeaderOffset + kLocalHeaderMinSize > m_bytes.size()) {
        return std::nullopt;
    }
    const unsigned char* lh = &m_bytes[e.localHeaderOffset];
    const std::uint16_t nameLen = readU16(lh + 26);
    const std::uint16_t extraLen = readU16(lh + 28);
    const std::size_t dataOffset = e.localHeaderOffset + kLocalHeaderMinSize + nameLen + extraLen;
    if (dataOffset + e.compressedSize > m_bytes.size()) {
        return std::nullopt;
    }
    const char* in = reinterpret_cast<const char*>(&m_bytes[dataOffset]);

    if (e.method == 0) {
        if (e.compressedSize != e.uncompressedSize) {
            return std::nullopt;
        }
        return std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(in),
                                          reinterpret_cast<const unsigned char*>(in) +
                                              e.uncompressedSize);
    }
    if (e.method == 8) {
        std::vector<unsigned char> out(e.uncompressedSize);
        const int written = stbi_zlib_decode_noheader_buffer(
            reinterpret_cast<char*>(out.data()), static_cast<int>(out.size()), in,
            static_cast<int>(e.compressedSize));
        if (written != static_cast<int>(e.uncompressedSize)) {
            return std::nullopt;
        }
        return out;
    }
    return std::nullopt;
}

} // namespace cc
