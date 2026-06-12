#pragma once

#include "app/Window.hpp"
#include "core/ThreadPool.hpp"
#include "input/Input.hpp"
#include "player/Camera.hpp"
#include "player/Player.hpp"
#include "render/Hud.hpp"
#include "render/Renderer.hpp"
#include "world/Block.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"
#include "world/WorldList.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cc {

// Composition root and top-level state machine. Declaration order doubles as
// the dependency order: the pool is declared before the worlds so a world
// (whose destructor waits for its in-flight jobs) is torn down first.
class Application {
public:
    Application();

    void run();

private:
    enum class GameState { Menu, Playing, Paused };
    enum class MenuScreen { Main, Worlds };

    void enterMenu();
    void startGame(const WorldInfo& info);
    void setPaused(bool paused);

    void updateMenu(float frameDt, const glm::ivec2& fbSize);
    void drawMainScreen(const glm::ivec2& fbSize);
    void drawWorldsScreen(const glm::ivec2& fbSize);
    void updatePlaying(float frameDt, const glm::ivec2& fbSize, double& accumulator);
    void updatePaused(const glm::ivec2& fbSize);

    void handleGameplayInput(float frameDt, const RaycastHit& target);
    void drawDebugOverlay(const glm::ivec2& fbSize, const RaycastHit& target);
    void renderWorld(World& world, const glm::ivec2& fbSize, const glm::vec3& eye, float yawDeg,
                     float pitchDeg, float fogEnd, const std::optional<glm::ivec3>& highlight);

    // Immediate-mode button: draws into the HUD batch and reports a click.
    [[nodiscard]] bool button(const glm::ivec2& fbSize, float centerX, float bottomY,
                              std::string_view label);
    [[nodiscard]] glm::vec2 mouseUiPosition(const glm::ivec2& fbSize) const noexcept;

    void updateTitle(double now);

    static constexpr int kRenderDistance = 12;
    static constexpr int kMenuRenderDistance = 8;

    Window m_window;
    Input m_input;
    Renderer m_renderer;
    Hud m_hud;
    ThreadPool m_pool;
    std::unique_ptr<World> m_menuWorld;
    std::unique_ptr<World> m_world;
    Player m_player;
    Camera m_camera;

    GameState m_state = GameState::Menu;
    MenuScreen m_menuScreen = MenuScreen::Main;
    glm::vec3 m_menuEye{0.0f};
    double m_menuTime = 0.0;

    std::string m_nameField;
    std::vector<WorldInfo> m_worlds;
    WorldInfo m_currentWorld;
    bool m_showDebug = false;

    std::array<BlockType, 8> m_hotbar{BlockType::Stone, BlockType::Dirt,  BlockType::Grass,
                                      BlockType::Sand,  BlockType::Wood,  BlockType::Leaves,
                                      BlockType::Plank, BlockType::Snow};
    int m_selectedSlot = 0;
    float m_editCooldown = 0.0f;

    double m_lastTitleUpdate = 0.0;
    int m_framesSinceTitle = 0;
    double m_smoothedFps = 0.0;
};

} // namespace cc
