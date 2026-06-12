#include "app/Application.hpp"

#include "core/Log.hpp"
#include "gl/GlDebug.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <span>

namespace cc {
namespace {

constexpr double kFixedDt = 1.0 / 60.0;
constexpr float kReachDistance = 5.5f;
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

constexpr const char* kSavesRoot = "saves";
constexpr const char* kSettingsPath = "settings.txt";

constexpr std::array<int, 7> kRenderDistanceOptions{4, 6, 8, 10, 12, 14, 16};
constexpr std::array<int, 7> kFovOptions{60, 70, 75, 80, 90, 100, 110};

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
    : m_settings{Settings::load(kSettingsPath)},
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
                                          std::filesystem::path(kSavesRoot) / ".menu");
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
    const float spawnY = static_cast<float>(m_world->generator().surfaceHeight(8, 8)) + 2.5f;
    m_player = Player{glm::vec3{8.5f, spawnY, 8.5f}};
    m_currentWorld = info;
    m_worldTime = info.timeOfDay;

    m_state = GameState::Playing;
    m_window.setCursorCaptured(true);
    m_input.resetMouseDelta();
    logInfo(std::format("entering world '{}' (seed {})", info.name, info.seed));
}

void Application::enterSettings(GameState from) {
    m_settingsFrom = from;
    m_settingsCategory = SettingsCategory::Video;
    m_state = GameState::Settings;
}

void Application::leaveSettings() {
    m_settings.save(kSettingsPath);
    m_state = m_settingsFrom;
    if (m_state == GameState::Menu) {
        m_menuScreen = MenuScreen::Main;
    }
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
    m_renderer.render(Renderer::FrameParams{viewProj, eye, sky.skyColor, sky.sunDirection,
                                            fogEnd * 0.65f, fogEnd, sky.skyLight, highlight});
}

void Application::saveWorldMeta() {
    if (m_world != nullptr && !m_currentWorld.name.empty()) {
        m_currentWorld.timeOfDay = m_worldTime;
        worldlist::saveMeta(m_currentWorld);
    }
}

void Application::handleGameplayInput(float frameDt, const RaycastHit& target) {
    if (m_input.wasPressed(GLFW_KEY_F)) {
        m_player.toggleFly();
    }
    if (m_input.wasPressed(GLFW_KEY_F3)) {
        m_showDebug = !m_showDebug;
    }

    const glm::vec2 look = m_input.mouseDelta() * kMouseSensitivity * m_settings.sensitivity;
    m_player.addLook(look.x, m_settings.invertY ? look.y : -look.y);

    PlayerInput move;
    move.move.y = (m_input.isDown(GLFW_KEY_W) ? 1.0f : 0.0f) -
                  (m_input.isDown(GLFW_KEY_S) ? 1.0f : 0.0f);
    move.move.x = (m_input.isDown(GLFW_KEY_D) ? 1.0f : 0.0f) -
                  (m_input.isDown(GLFW_KEY_A) ? 1.0f : 0.0f);
    move.jump = m_input.isDown(GLFW_KEY_SPACE);
    move.descend = m_input.isDown(GLFW_KEY_LEFT_SHIFT);
    m_player.setInput(move);

    for (int key = GLFW_KEY_1; key <= GLFW_KEY_9; ++key) {
        if (m_input.wasPressed(key)) {
            m_selectedSlot = key - GLFW_KEY_1;
        }
    }
    const float scroll = m_input.scrollDelta();
    if (scroll != 0.0f) {
        const int slots = static_cast<int>(m_hotbar.size());
        m_selectedSlot = (m_selectedSlot - static_cast<int>(scroll) % slots + slots) % slots;
    }

    // Edge presses act instantly; holding repeats on a cooldown.
    m_editCooldown = std::max(m_editCooldown - frameDt, 0.0f);
    const bool breakNow = m_input.wasMousePressed(GLFW_MOUSE_BUTTON_LEFT) ||
                          (m_input.isMouseDown(GLFW_MOUSE_BUTTON_LEFT) && m_editCooldown == 0.0f);
    const bool placeNow = m_input.wasMousePressed(GLFW_MOUSE_BUTTON_RIGHT) ||
                          (m_input.isMouseDown(GLFW_MOUSE_BUTTON_RIGHT) && m_editCooldown == 0.0f);

    if (target.hit && breakNow) {
        if (m_world->setBlock(target.block, BlockType::Air)) {
            m_editCooldown = kEditRepeatDelay;
        }
    } else if (target.hit && placeNow) {
        if (target.previous != target.block && !m_player.intersectsBlock(target.previous) &&
            !blockInfo(m_world->blockAt(target.previous)).solid) {
            if (m_world->setBlock(target.previous,
                                  m_hotbar[static_cast<std::size_t>(m_selectedSlot)])) {
                m_editCooldown = kEditRepeatDelay;
            }
        }
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
        m_worlds = worldlist::list(kSavesRoot);
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

    const bool createClicked =
        button(fbSize, centerX, fieldY - kButtonHeight - 14.0f, "CREATE") ||
        m_input.wasPressed(GLFW_KEY_ENTER);
    if (createClicked) {
        startGame(worldlist::create(kSavesRoot, m_nameField, randomSeed()));
        return;
    }

    float listY = fieldY - 2.0f * (kButtonHeight + 14.0f) - 64.0f;
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
        *m_world, eye, Camera::direction(m_player.yaw(), m_player.pitch()), kReachDistance);

    handleGameplayInput(frameDt, target);

    accumulator += frameDt;
    while (accumulator >= kFixedDt) {
        m_player.fixedUpdate(static_cast<float>(kFixedDt), *m_world);
        accumulator -= kFixedDt;
    }

    const float alpha = static_cast<float>(accumulator / kFixedDt);
    renderWorld(*m_world, fbSize, m_player.eyePosition(alpha), m_player.yaw(), m_player.pitch(),
                fogEndFor(m_settings.renderDistance), skyStateAt(m_worldTime),
                target.hit ? std::optional<glm::ivec3>{target.block} : std::nullopt);

    m_hud.crosshair(fbSize);
    m_hud.hotbar(fbSize, m_selectedSlot, m_hotbar);
    if (m_showDebug) {
        drawDebugOverlay(fbSize, target);
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

    const std::array<std::string, 12> lines{
        std::format("claudecraft {} | {:.0f} fps ({:.2f} ms)", kDebugBuild ? "debug" : "release",
                    m_smoothedFps, m_smoothedFps > 0.0 ? 1000.0 / m_smoothedFps : 0.0),
        std::format("World: {} Seed: {}", m_currentWorld.name, m_currentWorld.seed),
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
    };

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
    constexpr float kTabWidth = 220.0f;
    const float videoX = centerX - kTabWidth * 0.5f - 12.0f;
    const float controlsX = centerX + kTabWidth * 0.5f + 12.0f;
    const float activeX = m_settingsCategory == SettingsCategory::Video ? videoX : controlsX;
    m_hud.rect(activeX - kTabWidth * 0.5f - 3.0f, tabY - 3.0f, kTabWidth + 6.0f,
               kButtonHeight + 6.0f, Hud::RectStyle::Bright);
    if (button(fbSize, videoX, tabY, "VIDEO", kTabWidth)) {
        m_settingsCategory = SettingsCategory::Video;
    }
    if (button(fbSize, controlsX, tabY, "CONTROLS", kTabWidth)) {
        m_settingsCategory = SettingsCategory::Controls;
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
        if (settingRow(fbSize, rowY, "FULLSCREEN", m_settings.fullscreen ? "ON" : "OFF")) {
            m_settings.fullscreen = !m_settings.fullscreen;
            m_window.setFullscreen(m_settings.fullscreen);
        }
    } else {
        if (settingRow(fbSize, rowY, "SENSITIVITY",
                       std::format("{:.0f}%", m_settings.sensitivity * 100.0f))) {
            m_settings.sensitivity += 0.2f;
            if (m_settings.sensitivity > 3.01f) {
                m_settings.sensitivity = 0.2f;
            }
        }
        rowY -= kRowStep;
        if (settingRow(fbSize, rowY, "INVERT Y", m_settings.invertY ? "ON" : "OFF")) {
            m_settings.invertY = !m_settings.invertY;
        }
    }

    if (button(fbSize, centerX, 24.0f, "BACK")) {
        leaveSettings();
    }
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
                setPaused(true);
                accumulator = 0.0;
                break;
            case GameState::Paused:
                setPaused(false);
                break;
            case GameState::Settings:
                leaveSettings();
                break;
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
            }

            m_hud.flush(fbSize, m_renderer.atlas());
        }

        m_window.swapBuffers();
        updateTitle(now);
    }

    if (m_world != nullptr) {
        logInfo("window closed; saving world");
        saveWorldMeta();
        m_world->saveModified();
    }
}

} // namespace cc
