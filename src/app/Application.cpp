#include "app/Application.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "gl/GlDebug.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <random>
#include <span>
#include <thread>
#include <utility>

namespace cc {
namespace {

constexpr double kFixedDt = 1.0 / 60.0;
constexpr float kEditRepeatDelay = 0.22f;
constexpr float kMouseSensitivity = 0.09f;
constexpr double kDayLengthSeconds = 600.0;
constexpr double kMenuTimeOfDay = 0.5; // Claude-colored sunset for menu backdrop

constexpr float kButtonWidth = 280.0f;
constexpr float kButtonHeight = 52.0f;
constexpr float kButtonTextScale = 3.0f;
constexpr std::size_t kMaxListedWorlds = 5;
constexpr std::size_t kMaxNameLength = 24;

constexpr float kMenuPanSpeed = 3.0f;
constexpr float kMenuCamHeight = 24.0f;

// Creates the data dir, migrates any legacy working-dir data into it, and
// ensures the saves subdir exists. Runs first so settings/saves load from it.
[[nodiscard]] std::filesystem::path prepareDataDir() {
    const std::filesystem::path root = paths::dataRoot();
    paths::migrateLegacy(root);
    std::error_code ec;
    std::filesystem::create_directories(root / "saves", ec);
    return root;
}

constexpr std::array<int, 7> kRenderDistanceOptions{4, 6, 8, 10, 12, 14, 16};
constexpr std::array<int, 7> kFovOptions{60, 70, 75, 80, 90, 100, 110};
// Frame-cap presets; 0 (last) is unlimited, so cycling lands on 540 then off.
constexpr std::array<int, 9> kMaxFpsOptions{30, 60, 120, 144, 165, 240, 360, 540, 0};

constexpr std::array<BlockType, Inventory::HotbarSize> kCreativePalette{
    BlockType::Stone, BlockType::Dirt,  BlockType::Grass,
    BlockType::Sand,  BlockType::Wood,  BlockType::Leaves,
    BlockType::Plank, BlockType::Snow,  BlockType::Glowstone};

struct BindRow {
    const char* label;
    int Keybinds::* member;
};
constexpr std::array<BindRow, 10> kBindRows{{
    {"FORWARD", &Keybinds::forward},
    {"BACK", &Keybinds::back},
    {"LEFT", &Keybinds::left},
    {"RIGHT", &Keybinds::right},
    {"JUMP", &Keybinds::jump},
    {"DESCEND", &Keybinds::descend},
    {"SPRINT", &Keybinds::sprint},
    {"FLY", &Keybinds::fly},
    {"INVENTORY", &Keybinds::inventory},
    {"DROP", &Keybinds::drop},
}};

// Blocks flagged no-drop yield nothing (Air signals that); grass block
// crumbles to dirt; everything else drops itself.
[[nodiscard]] BlockType dropTypeFor(BlockType broken) noexcept {
    if (!blockInfo(broken).dropsItem) {
        return BlockType::Air;
    }
    return broken == BlockType::Grass ? BlockType::Dirt : broken;
}

// Nearest dry-land column to (0,0): spiral out by Chebyshev rings until a
// surface sits above sea level (so it has open air above for the player), then
// stand on top of it. Uses the generator directly so it works before the
// spawn chunk has streamed in.
[[nodiscard]] glm::vec3 computeSpawn(const TerrainGenerator& gen) noexcept {
    for (int r = 0; r <= 96; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) {
                    continue; // walk only the ring at radius r
                }
                const int h = gen.surfaceHeight(dx, dz);
                if (h > TerrainGenerator::SeaLevel) {
                    return glm::vec3{static_cast<float>(dx) + 0.5f, static_cast<float>(h) + 2.0f,
                                     static_cast<float>(dz) + 0.5f};
                }
            }
        }
    }
    return glm::vec3{0.5f, static_cast<float>(gen.surfaceHeight(0, 0)) + 2.0f, 0.5f};
}

[[nodiscard]] std::uint32_t scatterHash(const glm::ivec3& cell) noexcept {
    return static_cast<std::uint32_t>(cell.x) * 73856093u ^
           static_cast<std::uint32_t>(cell.y) * 19349663u ^
           static_cast<std::uint32_t>(cell.z) * 83492791u;
}

// Advance to the option after `current` (nearest match), wrapping.
[[nodiscard]] int cycleOption(int current, std::span<const int> options) noexcept {
    std::size_t index = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i] == current) {
            index = i;
            break;
        }
    }
    return options[(index + 1) % options.size()];
}

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

[[nodiscard]] unsigned workerCount() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp(hw > 2 ? hw - 2 : 1u, 2u, 8u);
}

[[nodiscard]] float fogEndFor(int renderDistance) noexcept {
    return static_cast<float>(renderDistance * Chunk::SizeX) - 8.0f;
}

[[nodiscard]] std::uint32_t randomSeed() {
    return std::random_device{}();
}

} // namespace

Application::Application()
    : m_dataRoot{prepareDataDir()},
      m_settings{Settings::load(m_dataRoot / "settings.txt")},
      m_window{1600, 900, "claudecraft", kDebugBuild},
      m_input{m_window.handle()},
      m_renderer{},
      m_hud{},
      m_pool{workerCount()},
      m_player{glm::vec3{0.0f}},
      m_camera{static_cast<float>(m_settings.fovDeg), 16.0f / 9.0f, 0.1f, 600.0f} {
    if (kDebugBuild) {
        gl::enableDebugOutput();
    }
    m_window.setVsync(m_settings.vsync);
    m_window.setFullscreen(m_settings.fullscreen);
    logInfo(std::format("{} workers", m_pool.threadCount()));
    applyResourcePacks();
    enterMenu();
}

// The menu backdrop is a throwaway world: fresh random seed, smaller render
// distance, a save path that is never written (the world is never modified)
// and that the world list ignores.
void Application::enterMenu() {
    saveWorldMeta();
    m_world.reset(); // saves modified chunks before the meshes go
    m_renderer.clearChunkMeshes();

    const std::uint32_t menuSeed = randomSeed();
    m_menuWorld = std::make_unique<World>(menuSeed, kMenuRenderDistance, m_pool,
                                          m_dataRoot / "saves" / ".menu");
    m_menuWorld->setSmoothLighting(m_settings.smoothLighting);
    m_menuEye = glm::vec3{
        0.0f, static_cast<float>(m_menuWorld->generator().surfaceHeight(0, 0)) + kMenuCamHeight,
        0.0f};
    m_state = GameState::Menu;
    m_menuScreen = MenuScreen::Main;
    m_window.setCursorCaptured(false);
}

void Application::startGame(const WorldInfo& info) {
    m_menuWorld.reset();
    m_renderer.clearChunkMeshes();

    m_world = std::make_unique<World>(info.seed, m_settings.renderDistance, m_pool,
                                      info.directory);
    m_world->setSmoothLighting(m_settings.smoothLighting);
    m_worldSpawn = computeSpawn(m_world->generator()); // nearest open land to 0,0
    const glm::vec3 start = info.hasPlayer
                                ? glm::vec3{info.playerX, info.playerY, info.playerZ}
                                : m_worldSpawn;
    m_player = Player{start};
    if (info.hasPlayer) {
        m_player.setLook(info.yaw, info.pitch);
        m_player.restoreVitals(info.health, info.hunger, info.saturation, info.exhaustion,
                               info.air);
    }
    m_player.setSpeedMultiplier(m_settings.playerSpeed);
    m_player.setSurvival(info.mode == GameMode::Survival);
    m_currentWorld = info;
    m_worldTime = info.timeOfDay;

    m_inventory.clear();
    if (std::filesystem::exists(info.directory / "player.dat")) {
        m_inventory.loadFrom(info.directory / "player.dat");
    } else if (info.mode == GameMode::Creative) {
        // Fresh creative worlds start with the classic palette on the hotbar.
        for (std::size_t i = 0; i < kCreativePalette.size(); ++i) {
            m_inventory.slot(static_cast<int>(i)) = ItemStack{kCreativePalette[i], 1};
        }
    }
    m_drops.clear();
    m_mobs.loadFrom(info.directory / "mobs.dat");
    m_mobRng = static_cast<std::uint32_t>(info.seed) | 1u; // keep the stream non-zero
    m_mobSpawnTimer = 3.0f;
    m_inventoryOpen = false;
    m_held = ItemStack{};
    m_miningProgress = 0.0f;

    m_state = GameState::Playing;
    m_window.setCursorCaptured(true);
    m_input.resetMouseDelta();
    logInfo(std::format("entering world '{}' (seed {}, {})", info.name, info.seed,
                        info.mode == GameMode::Survival ? "survival" : "creative"));
}

void Application::enterSettings(GameState from) {
    m_settingsFrom = from;
    m_settingsCategory = SettingsCategory::Video;
    m_rebinding = -1;
    m_rebindHotbar = -1;
    m_state = GameState::Settings;
}

void Application::leaveSettings() {
    m_settings.save(m_dataRoot / "settings.txt");
    m_state = m_settingsFrom;
    if (m_state == GameState::Menu) {
        m_menuScreen = MenuScreen::Main;
    }
}

void Application::applyResourcePacks() {
    const std::filesystem::path packsRoot = m_dataRoot / "texture_packs";
    std::vector<std::filesystem::path> packPaths;
    packPaths.reserve(m_settings.resourcePacks.size());
    for (const std::string& name : m_settings.resourcePacks) {
        const std::filesystem::path path = resourcepacks::pathFor(packsRoot, name);
        if (std::filesystem::exists(path)) {
            packPaths.push_back(path);
        }
    }
    m_renderer.setResourcePacks(packPaths);
}

void Application::setPaused(bool paused) {
    m_state = paused ? GameState::Paused : GameState::Playing;
    m_window.setCursorCaptured(!paused);
    if (!paused) {
        m_input.resetMouseDelta();
    }
}

glm::vec2 Application::mouseUiPosition(const glm::ivec2& fbSize) const noexcept {
    // Cursor coords are window-relative (y down); the HUD is framebuffer-
    // relative (y up). The ratio matters on high-DPI scaling.
    const glm::ivec2 winSize = m_window.size();
    if (winSize.x <= 0 || winSize.y <= 0) {
        return glm::vec2{-1.0f};
    }
    const glm::vec2 cursor = m_input.mousePosition();
    const float sx = static_cast<float>(fbSize.x) / static_cast<float>(winSize.x);
    const float sy = static_cast<float>(fbSize.y) / static_cast<float>(winSize.y);
    return glm::vec2{cursor.x * sx, static_cast<float>(fbSize.y) - cursor.y * sy};
}

bool Application::button(const glm::ivec2& fbSize, float centerX, float bottomY,
                         std::string_view label, float width) {
    const float x = centerX - width * 0.5f;
    const glm::vec2 mouse = mouseUiPosition(fbSize);
    const bool hovered = mouse.x >= x && mouse.x <= x + width && mouse.y >= bottomY &&
                         mouse.y <= bottomY + kButtonHeight;

    if (hovered) {
        m_hud.rect(x - 3.0f, bottomY - 3.0f, width + 6.0f, kButtonHeight + 6.0f,
                   Hud::RectStyle::Bright);
    }
    m_hud.rect(x, bottomY, width, kButtonHeight, Hud::RectStyle::Dim);
    const float textW = Hud::textWidth(label, kButtonTextScale);
    m_hud.text(centerX - textW * 0.5f, bottomY + kButtonHeight * 0.5f + 4.0f * kButtonTextScale,
               kButtonTextScale, label);

    return hovered && m_input.wasMousePressed(GLFW_MOUSE_BUTTON_LEFT);
}

void Application::renderWorld(World& world, const glm::ivec2& fbSize, const glm::vec3& eye,
                              float yawDeg, float pitchDeg, float fogEnd, const SkyState& sky,
                              const std::optional<glm::ivec3>& highlight) {
    WorldUpdate update = world.update(eye);
    for (MeshUpload& upload : update.uploads) {
        m_renderer.uploadChunkMesh(upload.coord, std::move(upload.mesh));
    }
    for (const ChunkCoord coord : update.removed) {
        m_renderer.removeChunkMesh(coord);
    }

    m_camera.setAspect(static_cast<float>(fbSize.x) / static_cast<float>(fbSize.y));
    const glm::mat4 viewProj = m_camera.projection() * Camera::view(eye, yawDeg, pitchDeg);
    Renderer::FrameParams params{viewProj,       eye,         sky.skyColor, sky.sunDirection,
                                 fogEnd * 0.65f, fogEnd,      sky.skyLight, highlight};
    if (highlight.has_value()) {
        const BlockInfo& info = blockInfo(world.blockAt(*highlight));
        if (info.shape == BlockShape::Box) {
            const float in = static_cast<float>(info.inset) / 16.0f;
            params.highlightMin = {in, 0.0f, in};
            params.highlightMax = {1.0f - in, 1.0f, 1.0f - in};
        }
    }
    m_renderer.render(params);

    if (m_showChunkBorders && m_world != nullptr && &world == m_world.get()) {
        const ChunkCoord chunk = World::chunkCoordOf(static_cast<int>(std::floor(eye.x)),
                                                     static_cast<int>(std::floor(eye.z)));
        m_renderer.drawChunkBorders(viewProj, chunk);
    }
}

void Application::saveWorldMeta() {
    if (m_world == nullptr || m_currentWorld.name.empty()) {
        return;
    }
    m_currentWorld.timeOfDay = m_worldTime;
    const glm::vec3 pos = m_player.position();
    m_currentWorld.hasPlayer = true;
    m_currentWorld.playerX = pos.x;
    m_currentWorld.playerY = pos.y;
    m_currentWorld.playerZ = pos.z;
    m_currentWorld.yaw = m_player.yaw();
    m_currentWorld.pitch = m_player.pitch();
    m_currentWorld.health = m_player.health();
    m_currentWorld.hunger = m_player.hunger();
    m_currentWorld.saturation = m_player.saturation();
    m_currentWorld.exhaustion = m_player.exhaustion();
    m_currentWorld.air = m_player.air();
    worldlist::saveMeta(m_currentWorld);
    m_mobs.saveTo(m_currentWorld.directory / "mobs.dat");
    if (!m_held.empty()) {
        m_inventory.add(m_held.type, m_held.count);
        m_held = ItemStack{};
    }
    m_inventory.saveTo(m_currentWorld.directory / "player.dat");
}

void Application::handleGameplayInput(float frameDt, const RaycastHit& target) {
    if (m_input.wasPressed(GLFW_KEY_F3)) {
        m_showDebug = !m_showDebug;
    }
    if (m_input.wasPressed(GLFW_KEY_G)) {
        m_showChunkBorders = !m_showChunkBorders;
    }
    const bool survival = m_currentWorld.mode == GameMode::Survival;
    const Keybinds& keys = m_settings.keys;
    if (m_input.wasPressed(keys.inventory)) {
        setInventoryOpen(!m_inventoryOpen);
    }
    if (m_inventoryOpen) {
        m_player.setInput(PlayerInput{}); // stop walking while the grid is up
        m_miningProgress = 0.0f;
        return;
    }

    if (m_input.wasPressed(keys.fly) && !survival) {
        m_player.toggleFly();
    }

    const glm::vec2 look = m_input.mouseDelta() * kMouseSensitivity * m_settings.sensitivity;
    m_player.addLook(look.x, m_settings.invertY ? look.y : -look.y);

    PlayerInput move;
    move.move.y = (m_input.isDown(keys.forward) ? 1.0f : 0.0f) -
                  (m_input.isDown(keys.back) ? 1.0f : 0.0f);
    move.move.x = (m_input.isDown(keys.right) ? 1.0f : 0.0f) -
                  (m_input.isDown(keys.left) ? 1.0f : 0.0f);
    move.jump = m_input.isDown(keys.jump);
    move.descend = m_input.isDown(keys.descend);
    move.sprint = m_input.isDown(keys.sprint);
    m_player.setInput(move);

    for (std::size_t i = 0; i < keys.hotbar.size(); ++i) {
        if (m_input.wasPressed(keys.hotbar[i])) {
            m_selectedSlot = static_cast<int>(i);
        }
    }
    const float scroll = m_input.scrollDelta();
    if (scroll != 0.0f) {
        constexpr int slots = Inventory::HotbarSize;
        m_selectedSlot = (m_selectedSlot - static_cast<int>(scroll) % slots + slots) % slots;
    }

    if (m_input.wasPressed(keys.drop)) {
        ItemStack& stack = m_inventory.slot(m_selectedSlot);
        if (!stack.empty()) {
            m_drops.throwOut(m_player.eyePosition(1.0f),
                             Camera::direction(m_player.yaw(), m_player.pitch()), stack.type);
            m_inventory.consumeOne(m_selectedSlot);
        }
    }

    handleBlockEdits(frameDt, target);
}

void Application::handleBlockEdits(float frameDt, const RaycastHit& target) {
    const bool survival = m_currentWorld.mode == GameMode::Survival;

    // Melee: a fresh left-click that lands on a mob hits it instead of mining.
    // Hand damage is tuned for playable fights rather than vanilla's 1 point.
    if (m_input.wasMousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        const glm::vec3 eye = m_player.eyePosition(1.0f);
        const glm::vec3 dir = Camera::direction(m_player.yaw(), m_player.pitch());
        std::vector<Mobs::Loot> loot;
        if (m_mobs.attack(eye, dir, m_settings.reach, 4.0f, m_mobRng, loot)) {
            for (const Mobs::Loot& l : loot) {
                const glm::ivec3 cell{static_cast<int>(std::floor(l.position.x)),
                                      static_cast<int>(std::floor(l.position.y)),
                                      static_cast<int>(std::floor(l.position.z))};
                for (int i = 0; i < l.count; ++i) {
                    m_drops.spawn(cell, l.type, scatterHash(cell) + static_cast<std::uint32_t>(i) * 0x9E3779B9u);
                }
            }
            m_player.addExhaustion(0.1f); // attacking costs hunger, like vanilla
            m_miningProgress = 0.0f;
            return;
        }
    }

    // Breaking. Creative: instant with a hold-repeat cooldown. Survival:
    // hold to mine — progress accumulates against the block's hardness and
    // resets when the target changes or the button is released.
    m_editCooldown = std::max(m_editCooldown - frameDt, 0.0f);
    const bool holdingBreak = m_input.isMouseDown(GLFW_MOUSE_BUTTON_LEFT);
    if (survival) {
        if (holdingBreak && target.hit && blockInfo(m_world->blockAt(target.block)).breakable) {
            if (m_miningCell != target.block) {
                m_miningCell = target.block;
                m_miningProgress = 0.0f;
            }
            m_miningProgress += frameDt;
            const BlockType type = m_world->blockAt(target.block);
            if (m_miningProgress >= blockInfo(type).hardness &&
                m_world->setBlock(target.block, BlockType::Air)) {
                if (const BlockType drop = dropTypeFor(type); drop != BlockType::Air) {
                    m_drops.spawn(target.block, drop, scatterHash(target.block));
                }
                m_player.addExhaustion(0.005f);
                m_miningProgress = 0.0f;
            }
        } else {
            m_miningProgress = 0.0f;
        }
    } else {
        const bool breakNow =
            m_input.wasMousePressed(GLFW_MOUSE_BUTTON_LEFT) ||
            (holdingBreak && m_editCooldown == 0.0f);
        if (target.hit && breakNow && m_world->setBlock(target.block, BlockType::Air)) {
            m_editCooldown = kEditRepeatDelay;
            return;
        }
    }

    // Placing. Survival consumes from the selected hotbar stack.
    const bool placeNow = m_input.wasMousePressed(GLFW_MOUSE_BUTTON_RIGHT) ||
                          (m_input.isMouseDown(GLFW_MOUSE_BUTTON_RIGHT) && m_editCooldown == 0.0f);
    if (!target.hit || !placeNow || target.previous == target.block ||
        m_player.intersectsBlock(target.previous) ||
        blockInfo(m_world->blockAt(target.previous)).solid) {
        return;
    }
    // Both modes place from the hotbar stack; only survival consumes it.
    const ItemStack& stack = m_inventory.slot(m_selectedSlot);
    if (stack.empty() || isItem(stack.type)) {
        return;
    }
    if (m_world->setBlock(target.previous, stack.type)) {
        if (survival) {
            m_inventory.consumeOne(m_selectedSlot);
        }
        m_editCooldown = kEditRepeatDelay;
    }
}

void Application::setInventoryOpen(bool open) {
    m_inventoryOpen = open;
    m_window.setCursorCaptured(!open);
    if (!open) {
        // Return whatever the cursor still holds to where it came from.
        if (!m_held.empty()) {
            if (m_heldFrom >= 0 && m_inventory.slot(m_heldFrom).empty()) {
                m_inventory.slot(m_heldFrom) = m_held;
            } else {
                m_inventory.add(m_held.type, m_held.count);
            }
            m_held = ItemStack{};
        }
        m_input.resetMouseDelta();
    }
}

// Slow diagonal pan with a drifting heading; height eases toward the
// terrain below so mountains don't clip the camera.
void Application::updateMenuBackdrop(float frameDt, const glm::ivec2& fbSize) {
    m_menuTime += frameDt;
    const auto t = static_cast<float>(m_menuTime);
    m_menuEye.x += kMenuPanSpeed * frameDt;
    m_menuEye.z += kMenuPanSpeed * 0.6f * frameDt;
    const float yaw = -55.0f + 12.0f * std::sin(t * 0.05f);
    const float targetY =
        static_cast<float>(m_menuWorld->generator().surfaceHeight(
            static_cast<int>(std::floor(m_menuEye.x)), static_cast<int>(std::floor(m_menuEye.z)))) +
        kMenuCamHeight;
    m_menuEye.y += (targetY - m_menuEye.y) * std::min(frameDt * 0.8f, 1.0f);

    renderWorld(*m_menuWorld, fbSize, m_menuEye, yaw, -18.0f, fogEndFor(kMenuRenderDistance),
                skyStateAt(kMenuTimeOfDay), std::nullopt);
}

void Application::updateMenu(float frameDt, const glm::ivec2& fbSize) {
    updateMenuBackdrop(frameDt, fbSize);

    if (m_menuScreen == MenuScreen::Main) {
        drawMainScreen(fbSize);
    } else {
        drawWorldsScreen(fbSize);
    }
}

void Application::drawMainScreen(const glm::ivec2& fbSize) {
    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    const float titleScale = 9.0f;
    const float titleW = Hud::textWidth("CLAUDECRAFT", titleScale);
    m_hud.text(width * 0.5f - titleW * 0.5f, height * 0.78f, titleScale, "CLAUDECRAFT");

    if (button(fbSize, width * 0.5f, height * 0.45f, "PLAY")) {
        m_worlds = worldlist::list(m_dataRoot / "saves");
        m_nameField.clear();
        m_menuScreen = MenuScreen::Worlds;
        return;
    }
    if (button(fbSize, width * 0.5f, height * 0.45f - kButtonHeight - 18.0f, "SETTINGS")) {
        enterSettings(GameState::Menu);
        return;
    }
    if (button(fbSize, width * 0.5f, height * 0.45f - 2.0f * (kButtonHeight + 18.0f), "QUIT")) {
        glfwSetWindowShouldClose(m_window.handle(), GLFW_TRUE);
    }
}

void Application::drawWorldsScreen(const glm::ivec2& fbSize) {
    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    const float centerX = width * 0.5f;

    // Name entry: typed characters append, backspace erases (key repeat
    // included), ENTER creates.
    for (const char c : m_input.typedText()) {
        if (m_nameField.size() < kMaxNameLength) {
            m_nameField.push_back(c);
        }
    }
    if (m_input.wasPressed(GLFW_KEY_BACKSPACE) && !m_nameField.empty()) {
        m_nameField.pop_back();
    }

    const float headerScale = 5.0f;
    const float headerW = Hud::textWidth("CREATE WORLD", headerScale);
    m_hud.text(centerX - headerW * 0.5f, height * 0.92f, headerScale, "CREATE WORLD");

    const float fieldW = 420.0f;
    const float fieldH = 46.0f;
    const float fieldY = height * 0.92f - 90.0f;
    m_hud.rect(centerX - fieldW * 0.5f, fieldY, fieldW, fieldH, Hud::RectStyle::Dim);
    const bool cursorOn = std::fmod(m_menuTime, 1.0) < 0.5;
    const std::string shown =
        m_nameField.empty() ? std::string{cursorOn ? "|" : ""}
                            : m_nameField + (cursorOn ? "|" : "");
    m_hud.text(centerX - fieldW * 0.5f + 12.0f, fieldY + fieldH * 0.5f + 4.0f * 2.5f, 2.5f, shown);

    if (button(fbSize, centerX, fieldY - kButtonHeight - 14.0f,
               m_createMode == GameMode::Survival ? "MODE: SURVIVAL" : "MODE: CREATIVE")) {
        m_createMode = m_createMode == GameMode::Survival ? GameMode::Creative
                                                          : GameMode::Survival;
    }
    const bool createClicked =
        button(fbSize, centerX, fieldY - 2.0f * (kButtonHeight + 14.0f), "CREATE") ||
        m_input.wasPressed(GLFW_KEY_ENTER);
    if (createClicked) {
        startGame(
            worldlist::create(m_dataRoot / "saves", m_nameField, randomSeed(), m_createMode));
        return;
    }

    float listY = fieldY - 3.0f * (kButtonHeight + 14.0f) - 64.0f;
    if (!m_worlds.empty()) {
        const float labelW = Hud::textWidth("LOAD WORLD", 3.5f);
        m_hud.text(centerX - labelW * 0.5f, listY + kButtonHeight + 44.0f, 3.5f, "LOAD WORLD");
    }
    for (std::size_t i = 0; i < m_worlds.size() && i < kMaxListedWorlds; ++i) {
        std::string label = m_worlds[i].name;
        if (label.size() > 16) {
            label.resize(16);
        }
        if (button(fbSize, centerX, listY - static_cast<float>(i) * (kButtonHeight + 12.0f),
                   label)) {
            startGame(m_worlds[i]);
            return;
        }
    }

    if (button(fbSize, centerX, 24.0f, "BACK")) {
        m_menuScreen = MenuScreen::Main;
    }
}

void Application::updatePlaying(float frameDt, const glm::ivec2& fbSize, double& accumulator) {
    m_worldTime = std::fmod(m_worldTime + frameDt / kDayLengthSeconds, 1.0);

    const glm::vec3 eye = m_player.eyePosition(1.0f);
    const RaycastHit target = raycast(
        *m_world, eye, Camera::direction(m_player.yaw(), m_player.pitch()), m_settings.reach);

    handleGameplayInput(frameDt, target);

    const bool survival = m_currentWorld.mode == GameMode::Survival;
    accumulator += frameDt;
    while (accumulator >= kFixedDt) {
        m_player.fixedUpdate(static_cast<float>(kFixedDt), *m_world);
        m_mobs.update(static_cast<float>(kFixedDt), *m_world, m_mobRng);
        for (const BlockType picked :
             m_drops.update(static_cast<float>(kFixedDt), *m_world, m_player.position())) {
            m_inventory.add(picked, 1);
        }
        accumulator -= kFixedDt;
    }
    updateMobSpawning(frameDt);

    if (m_player.dead()) {
        handleDeath();
        return;
    }

    const SkyState sky = skyStateAt(m_worldTime);
    const float alpha = static_cast<float>(accumulator / kFixedDt);
    const glm::vec3 renderEye = m_player.eyePosition(alpha);

    // Submerged camera: capture the world to an offscreen target and resolve it
    // through the underwater warp/tint. Dry, the world draws straight to screen.
    m_renderClock += frameDt;
    const glm::ivec3 eyeCell{static_cast<int>(std::floor(renderEye.x)),
                             static_cast<int>(std::floor(renderEye.y)),
                             static_cast<int>(std::floor(renderEye.z))};
    const bool underwater = isLiquid(m_world->blockAt(eyeCell));
    if (underwater) {
        m_post.begin(fbSize);
    }
    renderWorld(*m_world, fbSize, renderEye, m_player.yaw(), m_player.pitch(),
                fogEndFor(m_settings.renderDistance), sky,
                target.hit ? std::optional<glm::ivec3>{target.block} : std::nullopt);
    drawDrops(sky);
    drawMobs(sky);
    // Block-breaking crack overlay, advancing through the destroy stages as
    // mining progresses (replaces the old progress bar). Captured before the
    // underwater composite so it tints with the rest of the scene.
    if (survival && m_miningProgress > 0.0f && target.hit) {
        const float hardness = blockInfo(m_world->blockAt(target.block)).hardness;
        const float frac = std::clamp(m_miningProgress / std::max(hardness, 0.01f), 0.0f, 1.0f);
        const int stage = std::min(static_cast<int>(frac * TextureAtlas::DestroyStageCount),
                                   TextureAtlas::DestroyStageCount - 1);
        m_renderer.drawBlockBreak(target.block, stage);
    }
    if (underwater) {
        m_post.composite(static_cast<float>(m_renderClock));
    }

    m_hud.crosshair(fbSize);

    m_hud.hotbar(fbSize, m_selectedSlot, m_inventory.hotbar(), survival);

    if (survival) {
        drawSurvivalStatus(fbSize, underwater);
    }

    if (m_inventoryOpen) {
        drawInventoryUi(fbSize);
    }
    if (m_showDebug) {
        drawDebugOverlay(fbSize, target);
    }
}

// Vanilla-style status pips above the hotbar: hearts fill left-to-right,
// hunger right-to-left, and air bubbles appear over the hearts while diving.
void Application::drawSurvivalStatus(const glm::ivec2& fbSize, bool submerged) {
    constexpr float kPip = 16.0f;
    constexpr float kStep = 18.0f;
    const float cx = static_cast<float>(fbSize.x) * 0.5f;
    const float rowY = 70.0f; // just above the 48px hotbar (bottom 16)
    const float leftX = cx - 232.0f;
    const float rightX = cx + 232.0f - kPip;
    const bool gui = m_renderer.atlas().hasGuiIcons();

    for (int i = 0; i < 10; ++i) {
        const float x = leftX + static_cast<float>(i) * kStep;
        const float v = m_player.health() - static_cast<float>(i) * 2.0f;
        if (gui) {
            m_hud.icon(x, rowY, kPip, TextureAtlas::HeartBgTile);
            if (v >= 2.0f) {
                m_hud.icon(x, rowY, kPip, TextureAtlas::HeartFullTile);
            } else if (v >= 1.0f) {
                m_hud.icon(x, rowY, kPip, TextureAtlas::HeartHalfTile);
            }
        } else {
            m_hud.coloredRect(x, rowY, kPip, kPip, Hud::HeartEmpty);
            if (v >= 2.0f) {
                m_hud.coloredRect(x, rowY, kPip, kPip, Hud::HeartFull);
            } else if (v >= 1.0f) {
                m_hud.coloredRect(x, rowY, kPip * 0.5f, kPip, Hud::HeartFull);
            }
        }
    }
    for (int i = 0; i < 10; ++i) {
        const float x = rightX - static_cast<float>(i) * kStep;
        const float v = m_player.hunger() - static_cast<float>(i) * 2.0f;
        if (gui) {
            m_hud.icon(x, rowY, kPip, TextureAtlas::FoodBgTile);
            if (v >= 2.0f) {
                m_hud.icon(x, rowY, kPip, TextureAtlas::FoodFullTile);
            } else if (v >= 1.0f) {
                m_hud.icon(x, rowY, kPip, TextureAtlas::FoodHalfTile);
            }
        } else {
            m_hud.coloredRect(x, rowY, kPip, kPip, Hud::HungerEmpty);
            if (v >= 2.0f) {
                m_hud.coloredRect(x, rowY, kPip, kPip, Hud::HungerFull);
            } else if (v >= 1.0f) {
                m_hud.coloredRect(x, rowY, kPip * 0.5f, kPip, Hud::HungerFull);
            }
        }
    }
    if (submerged && m_player.air() < 0.999f) {
        const int bubbles = static_cast<int>(std::ceil(m_player.air() * 10.0f));
        for (int i = 0; i < bubbles; ++i) {
            const float x = leftX + static_cast<float>(i) * kStep;
            if (gui) {
                m_hud.icon(x, rowY + kStep, kPip, TextureAtlas::AirTile);
            } else {
                m_hud.coloredRect(x, rowY + kStep, kPip, kPip, Hud::AirBubble);
            }
        }
    }
}

void Application::handleDeath() {
    const glm::vec3 pos = m_player.position();
    const glm::ivec3 cell{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
                          static_cast<int>(std::floor(pos.z))};
    std::uint32_t salt = 0;
    const auto dropStack = [&](ItemStack& stack) {
        for (int i = 0; i < stack.count; ++i) {
            m_drops.spawn(cell, stack.type, scatterHash(cell) + salt++ * 0x9E3779B9u);
        }
        stack = ItemStack{};
    };
    for (int i = 0; i < Inventory::Size; ++i) {
        if (!m_inventory.slot(i).empty()) {
            dropStack(m_inventory.slot(i));
        }
    }
    if (!m_held.empty()) {
        dropStack(m_held);
    }
    m_inventoryOpen = false;
    m_state = GameState::Dead;
    m_window.setCursorCaptured(false);
}

void Application::updateDead(const glm::ivec2& fbSize) {
    // The world stays visible (frozen) behind a dark wash and the death prompt.
    renderWorld(*m_world, fbSize, m_player.eyePosition(1.0f), m_player.yaw(), m_player.pitch(),
                fogEndFor(m_settings.renderDistance), skyStateAt(m_worldTime), std::nullopt);

    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    m_hud.rect(0.0f, 0.0f, width, height, Hud::RectStyle::Dim);

    const float titleScale = 6.0f;
    const float titleW = Hud::textWidth("YOU DIED", titleScale);
    m_hud.text(width * 0.5f - titleW * 0.5f, height * 0.68f, titleScale, "YOU DIED");

    if (button(fbSize, width * 0.5f, height * 0.42f, "RESPAWN")) {
        m_player.respawn(m_worldSpawn);
        m_state = GameState::Playing;
        m_window.setCursorCaptured(true);
        return;
    }
    if (button(fbSize, width * 0.5f, height * 0.42f - kButtonHeight - 18.0f, "QUIT TO MENU")) {
        enterMenu();
    }
}

void Application::drawDrops(const SkyState& sky) {
    const auto drops = m_drops.all();
    if (drops.empty()) {
        return;
    }
    std::vector<Renderer::DropDraw> draws;
    draws.reserve(drops.size());
    for (const Drop& drop : drops) {
        const glm::ivec3 cell{static_cast<int>(std::floor(drop.position.x)),
                              static_cast<int>(std::floor(drop.position.y)),
                              static_cast<int>(std::floor(drop.position.z))};
        const std::uint8_t packed = m_world->lightPackedAt(cell);
        const float skyPart =
            static_cast<float>(Chunk::skyLevel(packed)) / 15.0f * sky.skyLight;
        const float blockPart = static_cast<float>(Chunk::blockLevel(packed)) / 15.0f;
        const float bob = 0.06f * std::sin(drop.age * 2.5f);
        draws.push_back(Renderer::DropDraw{
            drop.position + glm::vec3{0.0f, bob, 0.0f},
            drop.type,
            std::max({skyPart, blockPart, 0.06f}),
        });
    }
    m_renderer.drawDrops(draws);
}

void Application::drawMobs(const SkyState& sky) {
    const auto mobs = m_mobs.all();
    if (mobs.empty()) {
        return;
    }
    std::vector<Renderer::MobDraw> draws;
    draws.reserve(mobs.size());
    for (const MobEntity& mob : mobs) {
        // Sample at torso height: the feet cell is the solid block the mob
        // stands on, which carries no sky light and would render it black.
        const float torso = mob.position.y + mobInfo(mob.type).height * 0.5f;
        const glm::ivec3 cell{static_cast<int>(std::floor(mob.position.x)),
                              static_cast<int>(std::floor(torso)),
                              static_cast<int>(std::floor(mob.position.z))};
        const std::uint8_t packed = m_world->lightPackedAt(cell);
        const float skyPart = static_cast<float>(Chunk::skyLevel(packed)) / 15.0f * sky.skyLight;
        const float blockPart = static_cast<float>(Chunk::blockLevel(packed)) / 15.0f;
        draws.push_back(Renderer::MobDraw{mob.position, mob.yaw, mob.type,
                                          std::max({skyPart, blockPart, 0.06f}), mob.hurtFlash});
    }
    m_renderer.drawMobs(draws);
}

void Application::updateMobSpawning(float dt) {
    // Passive trickle: every few seconds, if the local population is thin, try
    // to drop one small herd of a random species on nearby grass.
    constexpr float kSpawnInterval = 8.0f;
    constexpr std::size_t kNearbyCap = 16;
    m_mobSpawnTimer -= dt;
    if (m_mobSpawnTimer > 0.0f) {
        return;
    }
    m_mobSpawnTimer = kSpawnInterval;
    m_mobs.cullDistant(m_player.position(), 110.0f);
    if (m_mobs.count() >= kNearbyCap) {
        return;
    }
    const auto species = static_cast<MobType>((m_mobRng >> 8) % 3u);
    const float angle = static_cast<float>(m_mobRng % 360u) * 0.01745329f;
    const glm::vec3 at = m_player.position() +
                         glm::vec3{std::cos(angle), 0.0f, std::sin(angle)} * 40.0f;
    m_mobs.spawnHerd(*m_world, at, species, 4, m_mobRng);
}

void Application::drawInventoryUi(const glm::ivec2& fbSize) {
    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    m_hud.rect(0.0f, 0.0f, width, height, Hud::RectStyle::Dim);

    constexpr float kSlot = 52.0f;
    constexpr float kGap = 6.0f;
    const float gridW = Inventory::HotbarSize * kSlot + (Inventory::HotbarSize - 1) * kGap;
    const float startX = width * 0.5f - gridW * 0.5f;
    const float gridTop = height * 0.5f + 80.0f;

    const bool creative = m_currentWorld.mode == GameMode::Creative;
    const float titleScale = 4.0f;
    m_hud.text(width * 0.5f - Hud::textWidth("INVENTORY", titleScale) * 0.5f,
               gridTop + (creative ? 290.0f : 70.0f), titleScale, "INVENTORY");

    const glm::vec2 mouse = mouseUiPosition(fbSize);
    const bool clicked = m_input.wasMousePressed(GLFW_MOUSE_BUTTON_LEFT);

    // Creative: an infinite source palette of every placeable block above
    // the grid. Clicking grabs a full stack onto the cursor.
    if (creative) {
        float px = startX;
        float py = gridTop + 2.0f * (kSlot + kGap) + 26.0f;
        m_hud.text(startX, py + kSlot + 30.0f, 2.5f, "ALL BLOCKS");
        for (int t = 1; t < static_cast<int>(BlockType::Count); ++t) {
            const auto type = static_cast<BlockType>(t);
            if (type == BlockType::Bedrock) {
                continue;
            }
            const ItemStack sample{type, 1};
            const bool hovered = mouse.x >= px && mouse.x <= px + kSlot && mouse.y >= py &&
                                 mouse.y <= py + kSlot;
            if (hovered) {
                m_hud.rect(px - 2.0f, py - 2.0f, kSlot + 4.0f, kSlot + 4.0f,
                           Hud::RectStyle::Bright);
            }
            m_hud.itemSlot(px, py, kSlot, sample, false);
            if (hovered && clicked) {
                m_held = ItemStack{type, kMaxStackSize};
                m_heldFrom = -1;
            }
            px += kSlot + kGap;
            if (px + kSlot > startX + gridW + 0.5f) {
                px = startX;
                py -= kSlot + kGap;
            }
        }
    }

    // Grid rows (slots 9..35) on top, hotbar row (0..8) separated below.
    for (int index = 0; index < Inventory::Size; ++index) {
        const int column = index % Inventory::HotbarSize;
        const bool isHotbar = index < Inventory::HotbarSize;
        const int gridRow = isHotbar ? 0 : (index / Inventory::HotbarSize) - 1;
        const float x = startX + static_cast<float>(column) * (kSlot + kGap);
        const float y = isHotbar ? gridTop - 3.0f * (kSlot + kGap) - 18.0f
                                 : gridTop - static_cast<float>(gridRow) * (kSlot + kGap);

        ItemStack& stack = m_inventory.slot(index);
        const bool hovered =
            mouse.x >= x && mouse.x <= x + kSlot && mouse.y >= y && mouse.y <= y + kSlot;
        if (hovered) {
            m_hud.rect(x - 2.0f, y - 2.0f, kSlot + 4.0f, kSlot + 4.0f, Hud::RectStyle::Bright);
        }
        m_hud.itemSlot(x, y, kSlot, stack, true);

        if (!hovered || !clicked) {
            continue;
        }
        if (m_held.empty()) {
            if (!stack.empty()) {
                m_held = stack;
                stack = ItemStack{};
                m_heldFrom = index;
            }
        } else if (stack.empty()) {
            stack = m_held;
            m_held = ItemStack{};
        } else if (stack.type == m_held.type && stack.count < kMaxStackSize) {
            const int moved = std::min(m_held.count, kMaxStackSize - stack.count);
            stack.count += moved;
            m_held.count -= moved;
            if (m_held.count == 0) {
                m_held = ItemStack{};
            }
        } else {
            std::swap(stack, m_held);
            m_heldFrom = index;
        }
    }

    if (!m_held.empty()) {
        m_hud.itemSlot(mouse.x - kSlot * 0.5f, mouse.y - kSlot * 0.5f, kSlot, m_held, true);
    }
}

void Application::drawDebugOverlay(const glm::ivec2& fbSize, const RaycastHit& target) {
    constexpr float kScale = 2.0f;
    constexpr float kLineHeight = 26.0f;
    constexpr float kPad = 5.0f;

    const glm::vec3 pos = m_player.position();
    const glm::vec3 vel = m_player.velocity();
    const ChunkCoord chunk = World::chunkCoordOf(static_cast<int>(std::floor(pos.x)),
                                                 static_cast<int>(std::floor(pos.z)));

    // Compass quadrant from yaw; Camera::direction maps yaw 0 to +x (east).
    const float yaw = m_player.yaw() - 360.0f * std::floor(m_player.yaw() / 360.0f);
    const char* facing = "east";
    if (yaw >= 45.0f && yaw < 135.0f) {
        facing = "south";
    } else if (yaw >= 135.0f && yaw < 225.0f) {
        facing = "west";
    } else if (yaw >= 225.0f && yaw < 315.0f) {
        facing = "north";
    }

    const glm::vec3 eye = m_player.eyePosition(1.0f);
    const glm::ivec3 eyeCell{static_cast<int>(std::floor(eye.x)),
                             static_cast<int>(std::floor(eye.y)),
                             static_cast<int>(std::floor(eye.z))};
    const std::uint8_t eyeLight = m_world->lightPackedAt(eyeCell);
    // The hit cell is solid (light 0); the cell in front of the face is the
    // lit one the player actually sees.
    const std::uint8_t targetLight =
        target.hit ? m_world->lightPackedAt(target.previous) : 0;

    // Clock maps sunrise (day fraction 0) to 06:00.
    const double dayHours = std::fmod(m_worldTime * 24.0 + 6.0, 24.0);
    const int clockHour = static_cast<int>(dayHours);
    const int clockMinute = static_cast<int>((dayHours - clockHour) * 60.0);

    m_stats.update(glfwGetTime());
    const Renderer::GpuStats gpu = m_renderer.gpuStats();

    std::vector<std::string> lines{
        std::format("claudecraft {} | {:.0f} fps ({:.2f} ms)", kDebugBuild ? "debug" : "release",
                    m_smoothedFps, m_smoothedFps > 0.0 ? 1000.0 / m_smoothedFps : 0.0),
        std::format("World: {} Seed: {} [{}]", m_currentWorld.name, m_currentWorld.seed,
                    m_currentWorld.mode == GameMode::Survival ? "survival" : "creative"),
        std::format("time: {:02}:{:02} (day fraction {:.3f})", clockHour, clockMinute,
                    m_worldTime),
        std::format("biome: {}",
                    biomeName(m_world->generator().biomeAt(
                        static_cast<int>(std::floor(pos.x)),
                        static_cast<int>(std::floor(pos.z))))),
        std::format("xyz: {:.2f} / {:.2f} / {:.2f}", pos.x, pos.y, pos.z),
        std::format("chunk: {} {}  local: {} {}", chunk.x, chunk.z,
                    static_cast<int>(std::floor(pos.x)) & 15,
                    static_cast<int>(std::floor(pos.z)) & 15),
        std::format("facing: {} (yaw {:.1f} / pitch {:.1f})", facing, yaw, m_player.pitch()),
        std::format("vel: {:.2f} {:.2f} {:.2f}  [{}]", vel.x, vel.y, vel.z,
                    m_player.flying() ? "flying" : (m_player.onGround() ? "on ground" : "airborne")),
        std::format("light here: sky {} block {}", Chunk::skyLevel(eyeLight),
                    Chunk::blockLevel(eyeLight)),
        target.hit ? std::format("target: {} at {} {} {} (sky {} block {})",
                                 blockName(m_world->blockAt(target.block)), target.block.x,
                                 target.block.y, target.block.z, Chunk::skyLevel(targetLight),
                                 Chunk::blockLevel(targetLight))
                   : std::string{"target: none"},
        std::format("chunks: {} loaded, {} drawn, {} meshes", m_world->loadedChunkCount(),
                    m_renderer.drawnLastFrame(), m_renderer.meshCount()),
        std::format("workers: {}", m_pool.threadCount()),
        std::format("cpu: {:.0f}%  ram: {:.0f} MB proc / {:.0f} of {:.0f} MB",
                    m_stats.cpuPercent(), m_stats.processRamMB(), m_stats.usedRamMB(),
                    m_stats.totalRamMB()),
        std::format("gpu: {}", gpu.name),
    };
    if (gpu.totalVramMB >= 0) {
        lines.push_back(std::format("vram: {} / {} MB ({} free)", gpu.usedVramMB, gpu.totalVramMB,
                                    gpu.freeVramMB));
    } else if (gpu.freeVramMB >= 0) {
        lines.push_back(std::format("vram: {} MB free", gpu.freeVramMB));
    }

    float yTop = static_cast<float>(fbSize.y) - 10.0f;
    for (const std::string& line : lines) {
        const float width = Hud::textWidth(line, kScale);
        m_hud.rect(6.0f, yTop - kLineHeight + kPad, width + 2.0f * kPad, kLineHeight,
                   Hud::RectStyle::Dim);
        m_hud.text(6.0f + kPad, yTop, kScale, line);
        yTop -= kLineHeight;
    }
}

void Application::updatePaused(const glm::ivec2& fbSize) {
    // World keeps streaming (mesh uploads finish) but the simulation is
    // frozen: no physics steps, no look, no edits, no time advance.
    renderWorld(*m_world, fbSize, m_player.eyePosition(1.0f), m_player.yaw(), m_player.pitch(),
                fogEndFor(m_settings.renderDistance), skyStateAt(m_worldTime), std::nullopt);

    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    m_hud.rect(0.0f, 0.0f, width, height, Hud::RectStyle::Dim);

    const float titleScale = 6.0f;
    const float titleW = Hud::textWidth("PAUSED", titleScale);
    m_hud.text(width * 0.5f - titleW * 0.5f, height * 0.7f, titleScale, "PAUSED");

    if (button(fbSize, width * 0.5f, height * 0.45f, "RESUME")) {
        setPaused(false);
        return;
    }
    if (button(fbSize, width * 0.5f, height * 0.45f - kButtonHeight - 18.0f, "SETTINGS")) {
        enterSettings(GameState::Paused);
        return;
    }
    if (button(fbSize, width * 0.5f, height * 0.45f - 2.0f * (kButtonHeight + 18.0f),
               "QUIT TO MENU")) {
        enterMenu(); // World destructor saves modified chunks
        return;
    }
    if (m_showDebug) {
        drawDebugOverlay(fbSize, RaycastHit{});
    }
}

bool Application::settingRow(const glm::ivec2& fbSize, float bottomY, std::string_view label,
                             std::string_view value) {
    const float centerX = static_cast<float>(fbSize.x) * 0.5f;
    constexpr float kLabelScale = 2.5f;
    const float labelW = Hud::textWidth(label, kLabelScale);
    m_hud.text(centerX - 40.0f - labelW, bottomY + kButtonHeight * 0.5f + 4.0f * kLabelScale,
               kLabelScale, label);
    return button(fbSize, centerX + 170.0f, bottomY, value, 300.0f);
}

bool Application::keybindRow(const glm::ivec2& fbSize, float columnX, float bottomY,
                             std::string_view label, std::string_view value) {
    constexpr float kLabelScale = 2.2f;
    constexpr float kButtonW = 190.0f;
    const float labelW = Hud::textWidth(label, kLabelScale);
    m_hud.text(columnX - kButtonW * 0.5f - 16.0f - labelW,
               bottomY + kButtonHeight * 0.5f + 4.0f * kLabelScale, kLabelScale, label);
    return button(fbSize, columnX, bottomY, value, kButtonW);
}

void Application::updateSettings(float frameDt, const glm::ivec2& fbSize) {
    if (m_settingsFrom == GameState::Paused) {
        renderWorld(*m_world, fbSize, m_player.eyePosition(1.0f), m_player.yaw(),
                    m_player.pitch(), fogEndFor(m_settings.renderDistance),
                    skyStateAt(m_worldTime), std::nullopt);
    } else {
        updateMenuBackdrop(frameDt, fbSize);
    }

    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    m_hud.rect(0.0f, 0.0f, width, height, Hud::RectStyle::Dim);
    const float centerX = width * 0.5f;

    const float titleScale = 6.0f;
    const float titleW = Hud::textWidth("SETTINGS", titleScale);
    m_hud.text(centerX - titleW * 0.5f, height * 0.92f, titleScale, "SETTINGS");

    // Category tabs; the active one keeps a permanent highlight ring.
    const float tabY = height * 0.92f - 110.0f;
    constexpr float kTabWidth = 180.0f;
    constexpr float kTabGap = 14.0f;
    const std::array<std::pair<const char*, SettingsCategory>, 4> tabs{{
        {"VIDEO", SettingsCategory::Video},
        {"CONTROLS", SettingsCategory::Controls},
        {"PACKS", SettingsCategory::Packs},
        {"CHEATS", SettingsCategory::Cheats},
    }};
    const float tabSpan = (kTabWidth + kTabGap);
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const float tabX =
            centerX + (static_cast<float>(i) - (static_cast<float>(tabs.size()) - 1.0f) * 0.5f) *
                          tabSpan;
        if (m_settingsCategory == tabs[i].second) {
            m_hud.rect(tabX - kTabWidth * 0.5f - 3.0f, tabY - 3.0f, kTabWidth + 6.0f,
                       kButtonHeight + 6.0f, Hud::RectStyle::Bright);
        }
        if (button(fbSize, tabX, tabY, tabs[i].first, kTabWidth)) {
            m_settingsCategory = tabs[i].second;
            m_rebinding = -1;
            m_rebindHotbar = -1;
        }
    }

    float rowY = tabY - kButtonHeight - 60.0f;
    constexpr float kRowStep = 72.0f;
    if (m_settingsCategory == SettingsCategory::Video) {
        if (settingRow(fbSize, rowY, "RENDER DISTANCE",
                       std::format("{} CHUNKS", m_settings.renderDistance))) {
            m_settings.renderDistance =
                cycleOption(m_settings.renderDistance, kRenderDistanceOptions);
            if (m_world != nullptr) {
                m_world->setRenderDistance(m_settings.renderDistance);
            }
        }
        rowY -= kRowStep;
        if (settingRow(fbSize, rowY, "FOV", std::format("{}", m_settings.fovDeg))) {
            m_settings.fovDeg = cycleOption(m_settings.fovDeg, kFovOptions);
            m_camera.setFov(static_cast<float>(m_settings.fovDeg));
        }
        rowY -= kRowStep;
        if (settingRow(fbSize, rowY, "VSYNC", m_settings.vsync ? "ON" : "OFF")) {
            m_settings.vsync = !m_settings.vsync;
            m_window.setVsync(m_settings.vsync);
        }
        rowY -= kRowStep;
        if (settingRow(fbSize, rowY, "MAX FPS",
                       m_settings.maxFps == 0 ? std::string{"UNLIMITED"}
                                              : std::format("{}", m_settings.maxFps))) {
            m_settings.maxFps = cycleOption(m_settings.maxFps, kMaxFpsOptions);
        }
        rowY -= kRowStep;
        if (settingRow(fbSize, rowY, "FULLSCREEN", m_settings.fullscreen ? "ON" : "OFF")) {
            m_settings.fullscreen = !m_settings.fullscreen;
            m_window.setFullscreen(m_settings.fullscreen);
        }
        rowY -= kRowStep;
        if (settingRow(fbSize, rowY, "SMOOTH LIGHTING",
                       m_settings.smoothLighting ? "ON" : "OFF")) {
            m_settings.smoothLighting = !m_settings.smoothLighting;
            if (m_world != nullptr) {
                m_world->setSmoothLighting(m_settings.smoothLighting);
            }
            if (m_menuWorld != nullptr) {
                m_menuWorld->setSmoothLighting(m_settings.smoothLighting);
            }
        }
    } else if (m_settingsCategory == SettingsCategory::Packs) {
        drawPackSettings(fbSize, rowY);
    } else if (m_settingsCategory == SettingsCategory::Cheats) {
        constexpr float kCheatStep = 64.0f;
        if (settingRow(fbSize, rowY, "PLAYER SPEED",
                       std::format("{:.1f}x", m_settings.playerSpeed))) {
            m_settings.playerSpeed += 0.5f;
            if (m_settings.playerSpeed > 8.01f) {
                m_settings.playerSpeed = 0.5f;
            }
            m_player.setSpeedMultiplier(m_settings.playerSpeed);
        }
        rowY -= kCheatStep;
        if (settingRow(fbSize, rowY, "REACH", std::format("{:.0f} blocks", m_settings.reach))) {
            m_settings.reach += 1.0f;
            if (m_settings.reach > 12.01f) {
                m_settings.reach = 3.0f;
            }
        }
    } else {
        // Finish a pending rebind with the next pressed key (ESC cancels via
        // the top-level handler). Only one capture is active at a time.
        const int pressed = m_input.lastKeyPressed();
        if (pressed != -1 && pressed != GLFW_KEY_ESCAPE) {
            if (m_rebinding >= 0) {
                m_settings.keys.*kBindRows[static_cast<std::size_t>(m_rebinding)].member = pressed;
                m_rebinding = -1;
            } else if (m_rebindHotbar >= 0) {
                m_settings.keys.hotbar[static_cast<std::size_t>(m_rebindHotbar)] = pressed;
                m_rebindHotbar = -1;
            }
        }

        constexpr float kBindStep = 52.0f;
        const float leftX = centerX - 235.0f;
        const float rightX = centerX + 235.0f;

        // Left column: mouse options then the action binds.
        float leftY = rowY;
        if (keybindRow(fbSize, leftX, leftY, "SENSITIVITY",
                       std::format("{:.0f}%", m_settings.sensitivity * 100.0f))) {
            m_settings.sensitivity += 0.2f;
            if (m_settings.sensitivity > 3.01f) {
                m_settings.sensitivity = 0.2f;
            }
        }
        leftY -= kBindStep;
        if (keybindRow(fbSize, leftX, leftY, "INVERT Y", m_settings.invertY ? "ON" : "OFF")) {
            m_settings.invertY = !m_settings.invertY;
        }
        for (std::size_t i = 0; i < kBindRows.size(); ++i) {
            leftY -= kBindStep;
            const bool capturing = m_rebinding == static_cast<int>(i);
            if (keybindRow(fbSize, leftX, leftY, kBindRows[i].label,
                           capturing ? "PRESS A KEY"
                                     : keyName(m_settings.keys.*kBindRows[i].member))) {
                m_rebinding = capturing ? -1 : static_cast<int>(i);
                m_rebindHotbar = -1;
            }
        }

        // Right column: the nine hotbar-slot binds.
        float rightY = rowY;
        for (std::size_t i = 0; i < m_settings.keys.hotbar.size(); ++i) {
            const bool capturing = m_rebindHotbar == static_cast<int>(i);
            if (keybindRow(fbSize, rightX, rightY, std::format("HOTBAR {}", i + 1),
                           capturing ? "PRESS A KEY"
                                     : keyName(m_settings.keys.hotbar[i]))) {
                m_rebindHotbar = capturing ? -1 : static_cast<int>(i);
                m_rebinding = -1;
            }
            rightY -= kBindStep;
        }
    }

    if (button(fbSize, centerX, 24.0f, "BACK")) {
        leaveSettings();
    }
}

void Application::drawPackSettings(const glm::ivec2& fbSize, float topY) {
    const float centerX = static_cast<float>(fbSize.x) * 0.5f;
    constexpr float kNameScale = 2.0f;
    constexpr float kRowStep = 56.0f;
    const float nameX = centerX - 380.0f;
    const auto label = [&](float bottomY, const std::string& text) {
        m_hud.text(nameX, bottomY + kButtonHeight * 0.5f + 4.0f * kNameScale, kNameScale, text);
    };

    // Deferred so we never mutate the list mid-draw; one action per frame.
    int moveUp = -1;
    int moveDown = -1;
    int removeAt = -1;
    std::string addName;

    float rowY = topY;
    m_hud.text(nameX, rowY + kButtonHeight, kNameScale, "ENABLED");
    rowY -= kButtonHeight * 0.6f;

    const int enabledCount = static_cast<int>(m_settings.resourcePacks.size());
    if (enabledCount == 0) {
        label(rowY, "(none)");
        rowY -= kRowStep;
    }
    for (int i = 0; i < enabledCount; ++i) {
        label(rowY, std::format("{}. {}", i + 1, m_settings.resourcePacks[static_cast<std::size_t>(i)]));
        if (i > 0 && button(fbSize, centerX + 70.0f, rowY, "/\\", 80.0f)) {
            moveUp = i;
        }
        if (i < enabledCount - 1 && button(fbSize, centerX + 160.0f, rowY, "\\/", 80.0f)) {
            moveDown = i;
        }
        if (button(fbSize, centerX + 290.0f, rowY, "DISABLE", 140.0f)) {
            removeAt = i;
        }
        rowY -= kRowStep;
    }

    rowY -= kButtonHeight * 0.4f;
    m_hud.text(nameX, rowY + kButtonHeight, kNameScale, "AVAILABLE");
    rowY -= kButtonHeight * 0.6f;

    const std::vector<std::string> avail = resourcepacks::available(m_dataRoot / "texture_packs");
    bool anyDisabled = false;
    for (const std::string& name : avail) {
        if (std::find(m_settings.resourcePacks.begin(), m_settings.resourcePacks.end(), name) !=
            m_settings.resourcePacks.end()) {
            continue;
        }
        anyDisabled = true;
        label(rowY, name);
        if (button(fbSize, centerX + 290.0f, rowY, "ADD", 140.0f)) {
            addName = name;
        }
        rowY -= kRowStep;
    }
    if (!anyDisabled) {
        label(rowY, avail.empty() ? "(put .zip packs in texture_packs/)" : "(all enabled)");
    }

    if (moveUp >= 0) {
        std::swap(m_settings.resourcePacks[static_cast<std::size_t>(moveUp)],
                  m_settings.resourcePacks[static_cast<std::size_t>(moveUp - 1)]);
    } else if (moveDown >= 0) {
        std::swap(m_settings.resourcePacks[static_cast<std::size_t>(moveDown)],
                  m_settings.resourcePacks[static_cast<std::size_t>(moveDown + 1)]);
    } else if (removeAt >= 0) {
        m_settings.resourcePacks.erase(m_settings.resourcePacks.begin() + removeAt);
    } else if (!addName.empty()) {
        m_settings.resourcePacks.insert(m_settings.resourcePacks.begin(), std::move(addName));
    } else {
        return;
    }
    m_settings.save(m_dataRoot / "settings.txt");
    applyResourcePacks();
}

void Application::updateTitle(double now) {
    ++m_framesSinceTitle;
    if (now - m_lastTitleUpdate < 0.5) {
        return;
    }
    const double fps = m_framesSinceTitle / (now - m_lastTitleUpdate);
    m_smoothedFps = fps;
    if (m_world == nullptr) {
        m_window.setTitle(std::format("claudecraft | {:.0f} fps | menu", fps));
    } else {
        const glm::vec3 pos = m_player.position();
        m_window.setTitle(std::format(
            "claudecraft | {} | {:.0f} fps | ({:.1f}, {:.1f}, {:.1f}) | {} chunks, {} drawn{}{}",
            m_currentWorld.name, fps, pos.x, pos.y, pos.z, m_world->loadedChunkCount(),
            m_renderer.drawnLastFrame(), m_player.flying() ? " | fly" : "",
            m_state == GameState::Paused ? " | paused" : ""));
    }
    m_lastTitleUpdate = now;
    m_framesSinceTitle = 0;
}

void Application::run() {
    double lastTime = glfwGetTime();
    double accumulator = 0.0;

    while (!m_window.shouldClose()) {
        const double now = glfwGetTime();
        // Clamp huge frame gaps (debugger pauses, window drags) so the fixed
        // step doesn't spiral trying to catch up.
        const double frameDt = std::min(now - lastTime, 0.25);
        lastTime = now;

        m_input.beginFrame();
        glfwPollEvents();

        // State transitions happen before the state runs so a single ESC
        // press can't be consumed twice in one frame.
        if (m_input.wasPressed(GLFW_KEY_ESCAPE)) {
            switch (m_state) {
            case GameState::Menu:
                if (m_menuScreen == MenuScreen::Worlds) {
                    m_menuScreen = MenuScreen::Main;
                } else {
                    glfwSetWindowShouldClose(m_window.handle(), GLFW_TRUE);
                }
                break;
            case GameState::Playing:
                if (m_inventoryOpen) {
                    setInventoryOpen(false);
                } else {
                    setPaused(true);
                    accumulator = 0.0;
                }
                break;
            case GameState::Paused:
                setPaused(false);
                break;
            case GameState::Settings:
                if (m_rebinding >= 0 || m_rebindHotbar >= 0) {
                    m_rebinding = -1;
                    m_rebindHotbar = -1;
                } else {
                    leaveSettings();
                }
                break;
            case GameState::Dead:
                break; // must click respawn
            }
        }

        const glm::ivec2 fbSize = m_window.framebufferSize();
        if (fbSize.x > 0 && fbSize.y > 0) {
            glViewport(0, 0, fbSize.x, fbSize.y);
            m_hud.beginFrame();

            switch (m_state) {
            case GameState::Menu:
                updateMenu(static_cast<float>(frameDt), fbSize);
                break;
            case GameState::Playing:
                updatePlaying(static_cast<float>(frameDt), fbSize, accumulator);
                break;
            case GameState::Paused:
                updatePaused(fbSize);
                break;
            case GameState::Settings:
                updateSettings(static_cast<float>(frameDt), fbSize);
                break;
            case GameState::Dead:
                updateDead(fbSize);
                break;
            }

            m_hud.flush(fbSize, m_renderer.atlas());
        }

        m_window.swapBuffers();

        // Frame cap (vsync already paces when on). Sleep the bulk of the
        // remaining budget, then spin the last millisecond for tight pacing.
        if (m_settings.maxFps > 0 && !m_settings.vsync) {
            const double target = 1.0 / static_cast<double>(m_settings.maxFps);
            const double remaining = target - (glfwGetTime() - now);
            if (remaining > 0.0015) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.001));
            }
            while (glfwGetTime() - now < target) {
            }
        }

        updateTitle(now);
    }

    if (m_world != nullptr) {
        logInfo("window closed; saving world");
        saveWorldMeta();
        m_world->saveModified();
    }
}

} // namespace cc
