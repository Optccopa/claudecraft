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

constexpr float kMenuPanSpeed = 3.0f;
constexpr float kMenuCamHeight = 24.0f;

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

    // The menu backdrop is a throwaway world with a fresh random seed; the
    // actual game world (fixed seed, persistent saves) is built on Play.
    const std::uint32_t menuSeed = std::random_device{}();
    m_menuWorld = std::make_unique<World>(menuSeed, kMenuRenderDistance, m_pool);
    m_menuEye = glm::vec3{
        0.0f, static_cast<float>(m_menuWorld->generator().surfaceHeight(0, 0)) + kMenuCamHeight,
        0.0f};
    logInfo(std::format("menu seed {} | {} workers", menuSeed, m_pool.threadCount()));
}

void Application::startGame() {
    m_menuWorld.reset();
    m_renderer.clearChunkMeshes();

    m_world = std::make_unique<World>(kSeed, kRenderDistance, m_pool);
    const float spawnY = static_cast<float>(m_world->generator().surfaceHeight(8, 8)) + 2.5f;
    m_player = Player{glm::vec3{8.5f, spawnY, 8.5f}};

    m_state = GameState::Playing;
    m_window.setCursorCaptured(true);
    m_input.resetMouseDelta();
    logInfo(std::format("game world seed {} | render distance {}", kSeed, kRenderDistance));
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

    const auto width = static_cast<float>(fbSize.x);
    const auto height = static_cast<float>(fbSize.y);
    const float titleScale = 9.0f;
    const float titleW = Hud::textWidth("CLAUDECRAFT", titleScale);
    m_hud.text(width * 0.5f - titleW * 0.5f, height * 0.78f, titleScale, "CLAUDECRAFT");

    if (button(fbSize, width * 0.5f, height * 0.45f, "PLAY")) {
        startGame();
        return;
    }
    if (button(fbSize, width * 0.5f, height * 0.45f - kButtonHeight - 18.0f, "QUIT")) {
        glfwSetWindowShouldClose(m_window.handle(), GLFW_TRUE);
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
    if (button(fbSize, width * 0.5f, height * 0.45f - kButtonHeight - 18.0f, "SAVE AND QUIT")) {
        glfwSetWindowShouldClose(m_window.handle(), GLFW_TRUE);
    }
}

void Application::updateTitle(double now) {
    ++m_framesSinceTitle;
    if (now - m_lastTitleUpdate < 0.5) {
        return;
    }
    const double fps = m_framesSinceTitle / (now - m_lastTitleUpdate);
    if (m_state == GameState::Menu) {
        m_window.setTitle(std::format("claudecraft | {:.0f} fps | menu", fps));
    } else {
        const glm::vec3 pos = m_player.position();
        m_window.setTitle(std::format(
            "claudecraft | {:.0f} fps | ({:.1f}, {:.1f}, {:.1f}) | {} chunks, {} drawn{}{}", fps,
            pos.x, pos.y, pos.z, m_world->loadedChunkCount(), m_renderer.drawnLastFrame(),
            m_player.flying() ? " | fly" : "", m_state == GameState::Paused ? " | paused" : ""));
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
                glfwSetWindowShouldClose(m_window.handle(), GLFW_TRUE);
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
