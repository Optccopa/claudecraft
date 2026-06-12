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

#include <array>

namespace cc {

// Composition root. Declaration order doubles as the dependency order:
// the pool is declared before the world so the world (whose destructor waits
// for its in-flight jobs) is torn down first.
class Application {
public:
    Application();

    void run();

private:
    void handleInput(float frameDt, const RaycastHit& target);
    void updateTitle(double now);

    static constexpr std::uint32_t kSeed = 1337;
    static constexpr int kRenderDistance = 12;

    Window m_window;
    Input m_input;
    Renderer m_renderer;
    Hud m_hud;
    ThreadPool m_pool;
    World m_world;
    Player m_player;
    Camera m_camera;

    std::array<BlockType, 8> m_hotbar{BlockType::Stone, BlockType::Dirt,  BlockType::Grass,
                                      BlockType::Sand,  BlockType::Wood,  BlockType::Leaves,
                                      BlockType::Plank, BlockType::Snow};
    int m_selectedSlot = 0;
    float m_editCooldown = 0.0f;

    double m_lastTitleUpdate = 0.0;
    int m_framesSinceTitle = 0;
};

} // namespace cc
