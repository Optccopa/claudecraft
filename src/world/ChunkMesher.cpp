#include "world/ChunkMesher.hpp"

#include <array>
#include <cstdint>

namespace cc {
namespace {

// Per face direction: normal axis/sign plus the two in-plane axes chosen so
// that uAxis x vAxis == normal, which makes the emitted quads wind CCW when
// seen from outside. texRight/texUp pick which world axes drive the texcoords
// so side textures always stand upright (v follows world Y on side faces).
struct FaceBasis {
    int normalAxis;
    int sign;
    int uAxis;
    int vAxis;
    int texRight;
    int texUp;
};

constexpr std::array<FaceBasis, 6> kFaces{{
    {0, +1, 1, 2, 2, 1}, // +x
    {0, -1, 2, 1, 2, 1}, // -x
    {1, +1, 2, 0, 0, 2}, // +y
    {1, -1, 0, 2, 0, 2}, // -y
    {2, +1, 0, 1, 0, 1}, // +z
    {2, -1, 1, 0, 0, 1}, // -z
}};

constexpr std::array<int, 3> kAxisSize{Chunk::SizeX, Chunk::SizeY, Chunk::SizeZ};

struct MaskCell {
    BlockType block = BlockType::Air;
    std::uint8_t ao = 0; // four 2-bit corner values: (du,dv) = 00,10,01,11
    std::array<std::uint8_t, 4> sky{};        // smoothed per-corner light, 0..15
    std::array<std::uint8_t, 4> blockLight{}; // corner order matches ao bits

    [[nodiscard]] friend bool operator==(const MaskCell&, const MaskCell&) = default;
    [[nodiscard]] bool empty() const noexcept { return block == BlockType::Air; }
};

[[nodiscard]] bool occludes(BlockType type) noexcept {
    return blockInfo(type).opaque;
}

// Classic flat-shaded voxel AO (Lysenko): a corner with both edge neighbours
// solid is fully occluded regardless of the diagonal.
[[nodiscard]] std::uint8_t cornerAo(bool side1, bool side2, bool corner) noexcept {
    if (side1 && side2) {
        return 0;
    }
    return static_cast<std::uint8_t>(3 - (static_cast<int>(side1) + static_cast<int>(side2) +
                                          static_cast<int>(corner)));
}

[[nodiscard]] std::uint8_t faceTile(BlockType type, int normalAxis, int sign) noexcept {
    const BlockInfo& info = blockInfo(type);
    if (normalAxis == 1) {
        return sign > 0 ? info.topTile : info.bottomTile;
    }
    return info.sideTile;
}

void emitQuad(ChunkMeshData& mesh, const FaceBasis& face, const std::array<int, 3>& base,
              int width, int height, const MaskCell& cell) {
    std::array<float, 3> p0{};
    for (int axis = 0; axis < 3; ++axis) {
        p0[static_cast<std::size_t>(axis)] = static_cast<float>(base[static_cast<std::size_t>(axis)]);
    }
    // The face lies on the far side of the cell when the normal is positive.
    if (face.sign > 0) {
        p0[static_cast<std::size_t>(face.normalAxis)] += 1.0f;
    }

    std::array<std::array<float, 3>, 4> pos{p0, p0, p0, p0};
    pos[1][static_cast<std::size_t>(face.uAxis)] += static_cast<float>(width);
    pos[2][static_cast<std::size_t>(face.uAxis)] += static_cast<float>(width);
    pos[2][static_cast<std::size_t>(face.vAxis)] += static_cast<float>(height);
    pos[3][static_cast<std::size_t>(face.vAxis)] += static_cast<float>(height);

    // Corner order in the mask is (du,dv) = 00,10,01,11; vertices run
    // v0=00, v1=10, v2=11, v3=01 around the quad.
    constexpr std::array<std::size_t, 4> kVertexCorner{0, 1, 3, 2};
    const std::array<std::uint8_t, 4> ao{
        static_cast<std::uint8_t>(cell.ao & 3u),
        static_cast<std::uint8_t>((cell.ao >> 2) & 3u),
        static_cast<std::uint8_t>((cell.ao >> 6) & 3u),
        static_cast<std::uint8_t>((cell.ao >> 4) & 3u),
    };

    const std::uint32_t tile = faceTile(cell.block, face.normalAxis, face.sign);
    const std::uint32_t normalIndex =
        static_cast<std::uint32_t>(face.normalAxis * 2 + (face.sign > 0 ? 0 : 1));

    const auto baseIndex = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& p = pos[i];
        const std::size_t corner = kVertexCorner[i];
        const std::uint32_t data =
            tile | (static_cast<std::uint32_t>(ao[i]) << 8) | (normalIndex << 10) |
            (static_cast<std::uint32_t>(cell.sky[corner]) << 13) |
            (static_cast<std::uint32_t>(cell.blockLight[corner]) << 17);
        // Positions are emitted in sixteenths (full-cube corners are integers,
        // so ×16 is exact); UVs stay in block units for the fract() atlas trick.
        mesh.vertices.push_back(packChunkVertex(
            static_cast<int>(p[0]) * 16, static_cast<int>(p[1]) * 16, static_cast<int>(p[2]) * 16,
            static_cast<int>(p[static_cast<std::size_t>(face.texRight)]),
            static_cast<int>(p[static_cast<std::size_t>(face.texUp)]), data));
    }

    // Split the quad along the diagonal joining the brighter corner pair so
    // interpolated AO doesn't streak across the dark corner (anisotropy fix).
    if (ao[0] + ao[2] >= ao[1] + ao[3]) {
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(baseIndex + i);
        }
    } else {
        for (const std::uint32_t i : {1u, 2u, 3u, 1u, 3u, 0u}) {
            mesh.indices.push_back(baseIndex + i);
        }
    }
}

void meshDirection(const MeshInput& input, const FaceBasis& face, ChunkMeshData& opaque,
                   ChunkMeshData& water) {
    const int sizeN = kAxisSize[static_cast<std::size_t>(face.normalAxis)];
    const int sizeU = kAxisSize[static_cast<std::size_t>(face.uAxis)];
    const int sizeV = kAxisSize[static_cast<std::size_t>(face.vAxis)];

    std::array<int, 3> normal{};
    normal[static_cast<std::size_t>(face.normalAxis)] = face.sign;

    std::vector<MaskCell> mask(static_cast<std::size_t>(sizeU * sizeV));

    for (int layer = 0; layer < sizeN; ++layer) {
        // Build the visibility mask for this slice.
        for (int iv = 0; iv < sizeV; ++iv) {
            for (int iu = 0; iu < sizeU; ++iu) {
                MaskCell& cell = mask[static_cast<std::size_t>(iv * sizeU + iu)];
                cell = MaskCell{};

                std::array<int, 3> p{};
                p[static_cast<std::size_t>(face.normalAxis)] = layer;
                p[static_cast<std::size_t>(face.uAxis)] = iu;
                p[static_cast<std::size_t>(face.vAxis)] = iv;

                const BlockType block = input.at(p[0], p[1], p[2]);
                if (block == BlockType::Air || !isFullCube(block)) {
                    continue; // non-cube shapes are emitted in a separate pass
                }
                const BlockType neighbor =
                    input.at(p[0] + normal[0], p[1] + normal[1], p[2] + normal[2]);

                // Water only shows a face against air, so submerged terrain
                // renders through one translucent surface, not stacked layers.
                const bool isWater = block == BlockType::Water;
                if (isWater ? neighbor != BlockType::Air : occludes(neighbor)) {
                    continue;
                }

                cell.block = block;

                // Flat lighting: every corner takes the face cell's light and
                // full AO — cheaper, merges better, and the classic look.
                if (!input.smoothLighting) {
                    const std::uint8_t l =
                        input.lightAt(p[0] + normal[0], p[1] + normal[1], p[2] + normal[2]);
                    cell.sky.fill(Chunk::skyLevel(l));
                    cell.blockLight.fill(Chunk::blockLevel(l));
                    cell.ao = 0xFF;
                    continue;
                }

                std::uint8_t aoBits = 0;
                for (int corner = 0; corner < 4; ++corner) {
                    const int du = (corner & 1) ? 1 : -1;
                    const int dv = (corner & 2) ? 1 : -1;
                    std::array<int, 3> n = p;
                    n[static_cast<std::size_t>(face.normalAxis)] += face.sign;
                    std::array<int, 3> s1 = n;
                    std::array<int, 3> s2 = n;
                    std::array<int, 3> diag = n;
                    s1[static_cast<std::size_t>(face.uAxis)] += du;
                    s2[static_cast<std::size_t>(face.vAxis)] += dv;
                    diag[static_cast<std::size_t>(face.uAxis)] += du;
                    diag[static_cast<std::size_t>(face.vAxis)] += dv;

                    const bool b1 = occludes(input.at(s1[0], s1[1], s1[2]));
                    const bool b2 = occludes(input.at(s2[0], s2[1], s2[2]));
                    const bool bd = occludes(input.at(diag[0], diag[1], diag[2]));
                    aoBits |= static_cast<std::uint8_t>(cornerAo(b1, b2, bd) << (corner * 2));

                    // Smooth lighting: average the face-plane cells around
                    // this corner, skipping opaque ones (they hold no light
                    // and would double-darken what AO already covers).
                    int skySum = static_cast<int>(Chunk::skyLevel(input.lightAt(n[0], n[1], n[2])));
                    int blockSum =
                        static_cast<int>(Chunk::blockLevel(input.lightAt(n[0], n[1], n[2])));
                    int samples = 1;
                    if (!b1) {
                        const std::uint8_t l = input.lightAt(s1[0], s1[1], s1[2]);
                        skySum += Chunk::skyLevel(l);
                        blockSum += Chunk::blockLevel(l);
                        ++samples;
                    }
                    if (!b2) {
                        const std::uint8_t l = input.lightAt(s2[0], s2[1], s2[2]);
                        skySum += Chunk::skyLevel(l);
                        blockSum += Chunk::blockLevel(l);
                        ++samples;
                    }
                    if (!bd) {
                        const std::uint8_t l = input.lightAt(diag[0], diag[1], diag[2]);
                        skySum += Chunk::skyLevel(l);
                        blockSum += Chunk::blockLevel(l);
                        ++samples;
                    }
                    cell.sky[static_cast<std::size_t>(corner)] =
                        static_cast<std::uint8_t>(skySum / samples);
                    cell.blockLight[static_cast<std::size_t>(corner)] =
                        static_cast<std::uint8_t>(blockSum / samples);
                }
                cell.ao = isWater ? 0xFF : aoBits;
            }
        }

        // Greedy merge: grow each unvisited cell right as far as it stays
        // identical, then down while every cell in the row matches. Equal AO
        // is part of the match so merged quads keep correct corner shading.
        for (int iv = 0; iv < sizeV; ++iv) {
            for (int iu = 0; iu < sizeU;) {
                const MaskCell cell = mask[static_cast<std::size_t>(iv * sizeU + iu)];
                if (cell.empty()) {
                    ++iu;
                    continue;
                }

                int width = 1;
                while (iu + width < sizeU &&
                       mask[static_cast<std::size_t>(iv * sizeU + iu + width)] == cell) {
                    ++width;
                }

                int height = 1;
                bool grow = true;
                while (grow && iv + height < sizeV) {
                    for (int k = 0; k < width; ++k) {
                        if (mask[static_cast<std::size_t>((iv + height) * sizeU + iu + k)] != cell) {
                            grow = false;
                            break;
                        }
                    }
                    if (grow) {
                        ++height;
                    }
                }

                std::array<int, 3> base{};
                base[static_cast<std::size_t>(face.normalAxis)] = layer;
                base[static_cast<std::size_t>(face.uAxis)] = iu;
                base[static_cast<std::size_t>(face.vAxis)] = iv;

                emitQuad(cell.block == BlockType::Water ? water : opaque, face, base, width,
                         height, cell);

                for (int dv = 0; dv < height; ++dv) {
                    for (int du = 0; du < width; ++du) {
                        mask[static_cast<std::size_t>((iv + dv) * sizeU + iu + du)] = MaskCell{};
                    }
                }
                iu += width;
            }
        }
    }
}

struct ShapeVertex {
    int x16, y16, z16; // sixteenths
    int u, v;          // block-unit texcoords
    std::uint8_t tile;
};

// Packs flat per-cell light + full AO; the caller supplies the normal index.
[[nodiscard]] std::uint32_t shapeData(std::uint8_t tile, std::uint32_t normalIndex,
                                      std::uint8_t sky, std::uint8_t block) noexcept {
    return static_cast<std::uint32_t>(tile) | (3u << 8) | (normalIndex << 10) |
           (static_cast<std::uint32_t>(sky) << 13) | (static_cast<std::uint32_t>(block) << 17);
}

void emitShapeQuad(ChunkMeshData& mesh, const std::array<ShapeVertex, 4>& quad,
                   std::uint32_t normalIndex, std::uint8_t sky, std::uint8_t block,
                   bool doubleSided) {
    const auto baseIndex = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const ShapeVertex& vert : quad) {
        mesh.vertices.push_back(packChunkVertex(vert.x16, vert.y16, vert.z16, vert.u, vert.v,
                                                shapeData(vert.tile, normalIndex, sky, block)));
    }
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
        mesh.indices.push_back(baseIndex + i);
    }
    if (doubleSided) {
        for (const std::uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) {
            mesh.indices.push_back(baseIndex + i);
        }
    }
}

// Cross-plant billboard: two double-sided diagonal quads spanning the cell, so
// blades show from every angle. Flat-lit from the plant's own cell; the +y
// normal lets the sky term pick up overhead sun.
void emitCrossPlant(ChunkMeshData& mesh, int lx, int ly, int lz, std::uint8_t tile,
                    std::uint8_t sky, std::uint8_t block) {
    const int x = lx * 16, y = ly * 16, z = lz * 16;
    const std::array<std::array<ShapeVertex, 4>, 2> planes{{
        {{{x, y, z, 0, 0, tile},
          {x + 16, y, z + 16, 1, 0, tile},
          {x + 16, y + 16, z + 16, 1, 1, tile},
          {x, y + 16, z, 0, 1, tile}}},
        {{{x + 16, y, z, 0, 0, tile},
          {x, y, z + 16, 1, 0, tile},
          {x, y + 16, z + 16, 1, 1, tile},
          {x + 16, y + 16, z, 0, 1, tile}}},
    }};
    for (const auto& plane : planes) {
        emitShapeQuad(mesh, plane, 2u, sky, block, true);
    }
}

// Inset box (cactus): Minecraft's model — the four side planes are pulled in
// `inset` sixteenths along their own normal but span the full cell on the
// perpendicular axis, so they cross in a "+" and the texture's transparent
// spike edges line up with the cell corners. Side faces always show; the
// top/bottom cap (the inset square) is culled against an identical box or an
// opaque neighbour above/below so a stacked column reads as one piece.
void emitBox(ChunkMeshData& mesh, const MeshInput& input, int lx, int ly, int lz, BlockType type) {
    const BlockInfo& info = blockInfo(type);
    const std::uint8_t l = input.lightAt(lx, ly, lz);
    const std::uint8_t sky = Chunk::skyLevel(l);
    const std::uint8_t blk = Chunk::blockLevel(l);
    const int ox = lx * 16, oy = ly * 16, oz = lz * 16;
    const int a = info.inset, b = 16 - info.inset; // inset side planes
    const std::uint8_t side = info.sideTile, top = info.topTile, bottom = info.bottomTile;

    const std::array<ShapeVertex, 4> faceXPos{{{ox + b, oy, oz, 0, 0, side},
                                               {ox + b, oy + 16, oz, 0, 1, side},
                                               {ox + b, oy + 16, oz + 16, 1, 1, side},
                                               {ox + b, oy, oz + 16, 1, 0, side}}};
    const std::array<ShapeVertex, 4> faceXNeg{{{ox + a, oy, oz, 0, 0, side},
                                               {ox + a, oy, oz + 16, 1, 0, side},
                                               {ox + a, oy + 16, oz + 16, 1, 1, side},
                                               {ox + a, oy + 16, oz, 0, 1, side}}};
    const std::array<ShapeVertex, 4> faceZPos{{{ox, oy, oz + b, 0, 0, side},
                                               {ox + 16, oy, oz + b, 1, 0, side},
                                               {ox + 16, oy + 16, oz + b, 1, 1, side},
                                               {ox, oy + 16, oz + b, 0, 1, side}}};
    const std::array<ShapeVertex, 4> faceZNeg{{{ox, oy, oz + a, 0, 0, side},
                                               {ox, oy + 16, oz + a, 0, 1, side},
                                               {ox + 16, oy + 16, oz + a, 1, 1, side},
                                               {ox + 16, oy, oz + a, 1, 0, side}}};
    emitShapeQuad(mesh, faceXPos, 0u, sky, blk, false);
    emitShapeQuad(mesh, faceXNeg, 1u, sky, blk, false);
    emitShapeQuad(mesh, faceZPos, 4u, sky, blk, false);
    emitShapeQuad(mesh, faceZNeg, 5u, sky, blk, false);

    // Caps are the plus-shaped footprint of the crossed side planes: a full-x /
    // inset-z strip and an inset-x / full-z strip. Their union covers the column
    // and leaves only the four spike corners open, so the cap matches the sides
    // (a single inset square would leave an open rim and clip the spike edges).
    const BlockType above = input.at(lx, ly + 1, lz);
    const BlockType below = input.at(lx, ly - 1, lz);
    if (above != type && !occludes(above)) {
        const std::array<ShapeVertex, 4> capXStrip{{{ox + a, oy + 16, oz, 0, 0, top},
                                                    {ox + a, oy + 16, oz + 16, 0, 1, top},
                                                    {ox + b, oy + 16, oz + 16, 1, 1, top},
                                                    {ox + b, oy + 16, oz, 1, 0, top}}};
        const std::array<ShapeVertex, 4> capZStrip{{{ox, oy + 16, oz + a, 0, 0, top},
                                                    {ox, oy + 16, oz + b, 0, 1, top},
                                                    {ox + 16, oy + 16, oz + b, 1, 1, top},
                                                    {ox + 16, oy + 16, oz + a, 1, 0, top}}};
        emitShapeQuad(mesh, capXStrip, 2u, sky, blk, false);
        emitShapeQuad(mesh, capZStrip, 2u, sky, blk, false);
    }
    if (below != type && !occludes(below)) {
        const std::array<ShapeVertex, 4> capXStrip{{{ox + a, oy, oz, 0, 0, bottom},
                                                    {ox + b, oy, oz, 1, 0, bottom},
                                                    {ox + b, oy, oz + 16, 1, 1, bottom},
                                                    {ox + a, oy, oz + 16, 0, 1, bottom}}};
        const std::array<ShapeVertex, 4> capZStrip{{{ox, oy, oz + a, 0, 0, bottom},
                                                    {ox + 16, oy, oz + a, 1, 0, bottom},
                                                    {ox + 16, oy, oz + b, 1, 1, bottom},
                                                    {ox, oy, oz + b, 0, 1, bottom}}};
        emitShapeQuad(mesh, capXStrip, 3u, sky, blk, false);
        emitShapeQuad(mesh, capZStrip, 3u, sky, blk, false);
    }
}

// Non-cube blocks (cross billboards, inset boxes) are emitted by this single
// scan after the greedy cube passes, all into the opaque mesh.
void meshSpecialShapes(const MeshInput& input, ChunkMeshData& opaque) {
    for (int lx = 0; lx < Chunk::SizeX; ++lx) {
        for (int lz = 0; lz < Chunk::SizeZ; ++lz) {
            for (int ly = 0; ly < Chunk::SizeY; ++ly) {
                const BlockType block = input.at(lx, ly, lz);
                const BlockShape shape = blockInfo(block).shape;
                if (shape == BlockShape::Cross) {
                    const std::uint8_t l = input.lightAt(lx, ly, lz);
                    emitCrossPlant(opaque, lx, ly, lz, blockInfo(block).sideTile,
                                   Chunk::skyLevel(l), Chunk::blockLevel(l));
                } else if (shape == BlockShape::Box) {
                    emitBox(opaque, input, lx, ly, lz, block);
                }
            }
        }
    }
}

} // namespace

ChunkMesher::Result ChunkMesher::build(const MeshInput& input) {
    Result result;
    result.opaque.vertices.reserve(4096);
    result.opaque.indices.reserve(6144);
    for (const FaceBasis& face : kFaces) {
        meshDirection(input, face, result.opaque, result.water);
    }
    meshSpecialShapes(input, result.opaque);
    return result;
}

} // namespace cc
