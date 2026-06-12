#include "app/Application.hpp"

#include "core/Log.hpp"
#include "gl/GlDebug.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace cc {
namespace {

constexpr double kFixedDt = 1.0 / 60.0;
constexpr float kReachDistance = 5.5f;
constexpr float kEditRepeatDelay = 0.22f;
constexpr float kMouseSensitivity = 0.09f;
constexpr glm::vec3 kSkyColor{0.55f, 0.74f, 0.95f};

constexpr float kButtonWidth = 280.0f;
constexpr float kButtonHeight = 52.0f;
constexpr float kButtonTextScale = 3.0f;
constexpr std::size_t kMaxListedWorlds = 5;
constexpr std::size_t kMaxNameLength = 24;

constexpr float kMenuPanSpeed = 3.0f;
constexpr float kMenuCamHeight = 24.0f;

constexpr const char* kSavesRoot = "saves";

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
    : m_window{1600, 900, "claudecraft", kDebugBuild},
      m_input{m_window.handle()},
      m_renderer{},
      m_hud{},
      m_pool{workerCount()},
      m_player{glm::vec3{0.0f}},
      m_camera{75.0f, 16.0f / 9.0f, 0.1f, 600.0f} {
    if (kDebugBuild) {
        gl::enableDebugOutput();
    }
    logInfo(std::format("{} workers", m_pool.threadCount()));
    enterMenu();
}

// The menu backdrop is a throwaway world: fresh random seed, smaller render
// distance, a save path that is never written (the world is never modified)
// and that the world list ignores.
void Application::enterMenu() {
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

    m_world = std::make_unique<World>(info.seed, kRenderDistance, m_pool, info.directory);
    const float spawnY = static_cast<float>(m_world->generator().surfaceHeight(8, 8)) + 2.5f;
    m_player = Player{glm::vec3{8.5f, spawnY, 8.5f}};
    m_currentWorld = info;

    m_state = GameState::Playing;
    m_window.setCursorCaptured(true);
    m_input.resetMouseDelta();
    logInfo(std::format("entering world '{}' (seed {})", info.name, info.seed));
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
                         std::string_view label) {
    const float x = centerX - kButtonWidth * 0.5f;
    const glm::vec2 mouse = mouseUiPosition(fbSize);
    const bool hovered = mouse.x >= x && mouse.x <= x + kButtonWidth && mouse.y >= bottomY &&
                         mouse.y <= bottomY + kButtonHeight;

    if (hovered) {
        m_hud.rect(x - 3.0f, bottomY - 3.0f, kButtonWidth + 6.0f, kButtonHeight + 6.0f,
                   Hud::RectStyle::Bright);
    }
    m_hud.rect(x, bottomY, kButtonWidth, kButtonHeight, Hud::RectStyle::Dim);
    const float textW = Hud::textWidth(label, kButtonTextScale);
    m_hud.text(centerX - textW * 0.5f, bottomY + kButtonHeight * 0.5f + 4.0f * kButtonTextScale,
               kButtonTextScale, label);

    return hovered && m_input.wasMousePressed(GLFW_MOUSE_BUTTON_LEFT);
}

void Application::renderWorld(World& world, const glm::ivec2& fbSize, const glm::vec3& eye,
                              float yawDeg, float pitchDeg, float fogEnd,
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
    m_renderer.render(Renderer::FrameParams{viewProj, eye, kSkyColor, fogEnd * 0.65f, fogEnd,
                                            highlight});
}

void Application::handleGameplayInput(float frameDt, const RaycastHit& target) {
    if (m_input.wasPressed(GLFW_KEY_F)) {
        m_player.toggleFly();
    }
    if (m_input.wasPressed(GLFW_KEY_F3)) {
        m_showDebug = !m_showDebug;
    }

    const glm::vec2 look = m_input.mouseDelta() * kMouseSensitivity;
    m_player.addLook(look.x, -look.y);

    PlayerInput move;
    move.move.y = (m_input.isDown(GLFW_KEY_W) ? 1.0f : 0.0f) -
                  (m_input.isDown(GLFW_KEY_S) ? 1.0f : 0.0f);
    move.move.x = (m_input.isDown(GLFW_KEY_D) ? 1.0f : 0.0f) -
                  (m_input.isDown(GLFW_KEY_A) ? 1.0f : 0.0f);
    move.jump = m_input.isDown(GLFW_KEY_SPACE);
    move.descend = m_input.isDown(GLFW_KEY_LEFT_SHIFT);
    m_player.setInput(move);

    for (int key = GLFW_KEY_1; key <= GLFW_KEY_8; ++key) {
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

void Application::updateMenu(float frameDt, const glm::ivec2& fbSize) {
    m_menuTime += frameDt;

    // Slow diagonal pan with a drifting heading; height eases toward the
    // terrain below so mountains don't clip the camera.
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
                std::nullopt);

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
    if (button(fbSize, width * 0.5f, height * 0.45f - kButtonHeight - 18.0f, "QUIT")) {
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
                fogEndFor(kRenderDistance),
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

    const std::array<std::string, 9> lines{
        std::format("claudecraft {} | {:.0f} fps ({:.2f} ms)", kDebugBuild ? "debug" : "release",
                    m_smoothedFps, m_smoothedFps > 0.0 ? 1000.0 / m_smoothedFps : 0.0),
        std::format("world '{}' seed {}", m_currentWorld.name, m_currentWorld.seed),
        std::format("xyz: {:.2f} / {:.2f} / {:.2f}", pos.x, pos.y, pos.z),
        std::format("chunk: {} {}  local: {} {}", chunk.x, chunk.z,
                    static_cast<int>(std::floor(pos.x)) & 15,
                    static_cast<int>(std::floor(pos.z)) & 15),
        std::format("facing: {} (yaw {:.1f} / pitch {:.1f})", facing, yaw, m_player.pitch()),
        std::format("vel: {:.2f} {:.2f} {:.2f}  [{}]", vel.x, vel.y, vel.z,
                    m_player.flying() ? "flying" : (m_player.onGround() ? "on ground" : "airborne")),
        target.hit ? std::format("target: {} at {} {} {}", blockName(m_world->blockAt(target.block)),
                                 target.block.x, target.block.y, target.block.z)
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
    // frozen: no physics steps, no look, no edits.
    renderWorld(*m_world, fbSize, m_player.eyePosition(1.0f), m_player.yaw(), m_player.pitch(),
                fogEndFor(kRenderDistance), std::nullopt);

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
    if (button(fbSize, width * 0.5f, height * 0.45f - kButtonHeight - 18.0f, "QUIT TO MENU")) {
        enterMenu(); // World destructor saves modified chunks
        return;
    }
    if (m_showDebug) {
        drawDebugOverlay(fbSize, RaycastHit{});
    }
}

void Application::updateTitle(double now) {
    ++m_framesSinceTitle;
    if (now - m_lastTitleUpdate < 0.5) {
        return;
    }
    const double fps = m_framesSinceTitle / (now - m_lastTitleUpdate);
    m_smoothedFps = fps;
    if (m_state == GameState::Menu) {
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
            }

            m_hud.flush(fbSize, m_renderer.atlas());
        }

        m_window.swapBuffers();
        updateTitle(now);
    }

    if (m_world != nullptr) {
        logInfo("window closed; saving world");
        m_world->saveModified();
    }
}

} // namespace cc
