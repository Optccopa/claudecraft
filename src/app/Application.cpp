#include "app/Application.hpp"

#include "core/Log.hpp"
#include "gl/GlDebug.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace cc {
namespace {

constexpr double kFixedDt = 1.0 / 60.0;
constexpr float kReachDistance = 5.5f;
constexpr float kEditRepeatDelay = 0.22f;
constexpr float kMouseSensitivity = 0.09f;
constexpr glm::vec3 kSkyColor{0.55f, 0.74f, 0.95f};

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

[[nodiscard]] unsigned workerCount() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp(hw > 2 ? hw - 2 : 1u, 2u, 8u);
}

} // namespace

Application::Application()
    : m_window{1600, 900, "claudecraft", kDebugBuild},
      m_input{m_window.handle()},
      m_renderer{},
      m_hud{},
      m_pool{workerCount()},
      m_world{kSeed, kRenderDistance, m_pool},
      m_player{glm::vec3{8.5f, 0.0f, 8.5f}},
      m_camera{75.0f, 16.0f / 9.0f, 0.1f, 600.0f} {
    if (kDebugBuild) {
        gl::enableDebugOutput();
    }
    // Deterministic spawn: the generator knows the surface height before the
    // chunk itself is generated.
    const float spawnY = static_cast<float>(m_world.generator().surfaceHeight(8, 8)) + 2.5f;
    m_player = Player{glm::vec3{8.5f, spawnY, 8.5f}};
    logInfo(std::format("seed {} | render distance {} | {} workers", kSeed, kRenderDistance,
                        m_pool.threadCount()));
}

void Application::handleInput(float frameDt, const RaycastHit& target) {
    if (m_input.wasPressed(GLFW_KEY_ESCAPE)) {
        glfwSetWindowShouldClose(m_window.handle(), GLFW_TRUE);
    }
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
        if (m_world.setBlock(target.block, BlockType::Air)) {
            m_editCooldown = kEditRepeatDelay;
        }
    } else if (target.hit && placeNow) {
        if (target.previous != target.block && !m_player.intersectsBlock(target.previous) &&
            !blockInfo(m_world.blockAt(target.previous)).solid) {
            if (m_world.setBlock(target.previous,
                                 m_hotbar[static_cast<std::size_t>(m_selectedSlot)])) {
                m_editCooldown = kEditRepeatDelay;
            }
        }
    }
}

void Application::updateTitle(double now) {
    ++m_framesSinceTitle;
    if (now - m_lastTitleUpdate < 0.5) {
        return;
    }
    const double fps = m_framesSinceTitle / (now - m_lastTitleUpdate);
    const glm::vec3 pos = m_player.position();
    m_window.setTitle(std::format(
        "claudecraft | {:.0f} fps | ({:.1f}, {:.1f}, {:.1f}) | {} chunks, {} drawn{}", fps, pos.x,
        pos.y, pos.z, m_world.loadedChunkCount(), m_renderer.drawnLastFrame(),
        m_player.flying() ? " | fly" : ""));
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

        const glm::vec3 eye = m_player.eyePosition(1.0f);
        const RaycastHit target =
            raycast(m_world, eye, Camera::direction(m_player.yaw(), m_player.pitch()),
                    kReachDistance);

        handleInput(static_cast<float>(frameDt), target);

        accumulator += frameDt;
        while (accumulator >= kFixedDt) {
            m_player.fixedUpdate(static_cast<float>(kFixedDt), m_world);
            accumulator -= kFixedDt;
        }

        WorldUpdate update = m_world.update(m_player.position());
        for (MeshUpload& upload : update.uploads) {
            m_renderer.uploadChunkMesh(upload.coord, std::move(upload.mesh));
        }
        for (const ChunkCoord coord : update.removed) {
            m_renderer.removeChunkMesh(coord);
        }

        const glm::ivec2 fbSize = m_window.framebufferSize();
        if (fbSize.x > 0 && fbSize.y > 0) {
            glViewport(0, 0, fbSize.x, fbSize.y);
            m_camera.setAspect(static_cast<float>(fbSize.x) / static_cast<float>(fbSize.y));

            const float alpha = static_cast<float>(accumulator / kFixedDt);
            const glm::vec3 renderEye = m_player.eyePosition(alpha);
            const glm::mat4 viewProj =
                m_camera.projection() * Camera::view(renderEye, m_player.yaw(), m_player.pitch());

            const float fogEnd = static_cast<float>(kRenderDistance * Chunk::SizeX) - 8.0f;
            m_renderer.render(Renderer::FrameParams{
                viewProj, renderEye, kSkyColor, fogEnd * 0.65f, fogEnd,
                target.hit ? std::optional<glm::ivec3>{target.block} : std::nullopt});
            m_hud.render(fbSize, m_renderer.atlas(), m_selectedSlot, m_hotbar);
        }

        m_window.swapBuffers();
        updateTitle(now);
    }

    logInfo("window closed; saving world");
    m_world.saveModified();
}

} // namespace cc
