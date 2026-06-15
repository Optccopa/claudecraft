#pragma once

#include "app/Settings.hpp"
#include "app/Window.hpp"
#include "core/SystemStats.hpp"
#include "core/ThreadPool.hpp"
#include "input/Input.hpp"
#include "player/Camera.hpp"
#include "player/Inventory.hpp"
#include "player/Player.hpp"
#include "world/Drops.hpp"
#include "render/Hud.hpp"
#include "render/PostProcess.hpp"
#include "render/Renderer.hpp"
#include "render/Sky.hpp"
#include "world/Block.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"
#include "world/WorldList.hpp"

#include <array>
#include <filesystem>
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
    enum class GameState { Menu, Playing, Paused, Settings, Dead };
    enum class MenuScreen { Main, Worlds };
    enum class SettingsCategory { Video, Controls, Packs, Cheats };

    void enterMenu();
    void startGame(const WorldInfo& info);
    void setPaused(bool paused);
    void enterSettings(GameState from);
    void leaveSettings();

    void updateMenu(float frameDt, const glm::ivec2& fbSize);
    void updateMenuBackdrop(float frameDt, const glm::ivec2& fbSize);
    void drawMainScreen(const glm::ivec2& fbSize);
    void drawWorldsScreen(const glm::ivec2& fbSize);
    void updatePlaying(float frameDt, const glm::ivec2& fbSize, double& accumulator);
    void updatePaused(const glm::ivec2& fbSize);
    void updateSettings(float frameDt, const glm::ivec2& fbSize);
    void drawPackSettings(const glm::ivec2& fbSize, float topY);
    // Rebuilds the renderer atlas from the enabled packs (existing ones only).
    void applyResourcePacks();
    // One settings line: right-aligned label, clickable value button.
    [[nodiscard]] bool settingRow(const glm::ivec2& fbSize, float bottomY, std::string_view label,
                                  std::string_view value);
    // Label + value button centred on an arbitrary column x (for the two-column
    // CONTROLS layout). settingRow is this centred on the screen.
    [[nodiscard]] bool keybindRow(const glm::ivec2& fbSize, float columnX, float bottomY,
                                  std::string_view label, std::string_view value);

    void handleGameplayInput(float frameDt, const RaycastHit& target);
    void handleBlockEdits(float frameDt, const RaycastHit& target);
    void setInventoryOpen(bool open);
    void drawInventoryUi(const glm::ivec2& fbSize);
    void drawSurvivalStatus(const glm::ivec2& fbSize, bool submerged);
    // Death: scatter the whole inventory at the body and show the death screen.
    void handleDeath();
    void updateDead(const glm::ivec2& fbSize);
    void drawDrops(const SkyState& sky);
    void drawDebugOverlay(const glm::ivec2& fbSize, const RaycastHit& target);
    void renderWorld(World& world, const glm::ivec2& fbSize, const glm::vec3& eye, float yawDeg,
                     float pitchDeg, float fogEnd, const SkyState& sky,
                     const std::optional<glm::ivec3>& highlight);
    void saveWorldMeta();

    // Immediate-mode button: draws into the HUD batch and reports a click.
    [[nodiscard]] bool button(const glm::ivec2& fbSize, float centerX, float bottomY,
                              std::string_view label, float width = 280.0f);
    [[nodiscard]] glm::vec2 mouseUiPosition(const glm::ivec2& fbSize) const noexcept;

    void updateTitle(double now);

    static constexpr int kMenuRenderDistance = 8;

    std::filesystem::path m_dataRoot; // %LOCALAPPDATA%/.claudecraft; first so it's ready early
    Settings m_settings;              // loaded before the members below consume it
    Window m_window;
    Input m_input;
    Renderer m_renderer;
    PostProcess m_post;
    Hud m_hud;
    ThreadPool m_pool;
    std::unique_ptr<World> m_menuWorld;
    std::unique_ptr<World> m_world;
    Player m_player;
    glm::vec3 m_worldSpawn{0.0f}; // default spawn, used for death respawn
    Camera m_camera;

    GameState m_state = GameState::Menu;
    MenuScreen m_menuScreen = MenuScreen::Main;
    GameState m_settingsFrom = GameState::Menu;
    SettingsCategory m_settingsCategory = SettingsCategory::Video;
    int m_rebinding = -1;      // index into the keybind rows while capturing
    int m_rebindHotbar = -1;   // hotbar slot 0..8 while capturing its key
    double m_worldTime = 0.05; // day fraction; advances only while Playing
    double m_renderClock = 0.0; // wall-clock seconds for animated effects
    glm::vec3 m_menuEye{0.0f};
    double m_menuTime = 0.0;

    std::string m_nameField;
    std::vector<WorldInfo> m_worlds;
    WorldInfo m_currentWorld;
    bool m_showDebug = false;
    bool m_showChunkBorders = false;

    Inventory m_inventory;
    Drops m_drops;
    bool m_inventoryOpen = false;
    ItemStack m_held;       // stack picked up by the cursor in the inventory UI
    int m_heldFrom = -1;    // slot it came from (returned there on close)
    GameMode m_createMode = GameMode::Creative; // worlds-screen picker state
    glm::ivec3 m_miningCell{0};
    float m_miningProgress = 0.0f;
    int m_selectedSlot = 0;
    float m_editCooldown = 0.0f;

    double m_lastTitleUpdate = 0.0;
    int m_framesSinceTitle = 0;
    double m_smoothedFps = 0.0;
    SystemStats m_stats;
};

} // namespace cc
