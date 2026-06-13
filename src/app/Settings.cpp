#include "app/Settings.hpp"

#include "core/Log.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace cc {
namespace {

struct KeyEntry {
    std::string_view name; // settings-file key
    int Keybinds::* member;
};

constexpr std::array<KeyEntry, 9> kKeyEntries{{
    {"key.forward", &Keybinds::forward},
    {"key.back", &Keybinds::back},
    {"key.left", &Keybinds::left},
    {"key.right", &Keybinds::right},
    {"key.jump", &Keybinds::jump},
    {"key.descend", &Keybinds::descend},
    {"key.fly", &Keybinds::fly},
    {"key.inventory", &Keybinds::inventory},
    {"key.drop", &Keybinds::drop},
}};

} // namespace

Keybinds defaultKeybinds() noexcept {
    return Keybinds{GLFW_KEY_W,     GLFW_KEY_S,          GLFW_KEY_A, GLFW_KEY_D,
                    GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_F, GLFW_KEY_E,
                    GLFW_KEY_Q};
}

std::string_view keyName(int key) noexcept {
    // glfwGetKeyName covers printable keys; the rest need explicit names.
    switch (key) {
    case GLFW_KEY_SPACE: return "SPACE";
    case GLFW_KEY_LEFT_SHIFT: return "LEFT SHIFT";
    case GLFW_KEY_RIGHT_SHIFT: return "RIGHT SHIFT";
    case GLFW_KEY_LEFT_CONTROL: return "LEFT CTRL";
    case GLFW_KEY_RIGHT_CONTROL: return "RIGHT CTRL";
    case GLFW_KEY_LEFT_ALT: return "LEFT ALT";
    case GLFW_KEY_TAB: return "TAB";
    case GLFW_KEY_ENTER: return "ENTER";
    case GLFW_KEY_BACKSPACE: return "BACKSPACE";
    case GLFW_KEY_UP: return "UP";
    case GLFW_KEY_DOWN: return "DOWN";
    case GLFW_KEY_LEFT: return "LEFT";
    case GLFW_KEY_RIGHT: return "RIGHT";
    case GLFW_KEY_CAPS_LOCK: return "CAPS LOCK";
    default: break;
    }
    if (const char* name = glfwGetKeyName(key, 0); name != nullptr) {
        // Single static buffer is fine: callers consume the view immediately
        // and only one key name is ever displayed per row.
        static char upper[16];
        std::size_t i = 0;
        for (; name[i] != '\0' && i < sizeof(upper) - 1; ++i) {
            upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
        }
        upper[i] = '\0';
        return upper;
    }
    return "UNNAMED";
}

Settings Settings::load(const std::filesystem::path& path) {
    Settings settings;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream parts(line);
        std::string key;
        if (!(parts >> key)) {
            continue;
        }
        if (key == "renderDistance") {
            parts >> settings.renderDistance;
        } else if (key == "fovDeg") {
            parts >> settings.fovDeg;
        } else if (key == "vsync") {
            parts >> settings.vsync;
        } else if (key == "fullscreen") {
            parts >> settings.fullscreen;
        } else if (key == "smoothLighting") {
            parts >> settings.smoothLighting;
        } else if (key == "sensitivity") {
            parts >> settings.sensitivity;
        } else if (key == "invertY") {
            parts >> settings.invertY;
        } else if (key == "pack") {
            // Pack names contain spaces, so take the rest of the line verbatim.
            std::string name;
            std::getline(parts >> std::ws, name);
            if (!name.empty()) {
                settings.resourcePacks.push_back(std::move(name));
            }
        } else {
            for (const KeyEntry& entry : kKeyEntries) {
                if (key == entry.name) {
                    parts >> settings.keys.*entry.member;
                    break;
                }
            }
        }
    }
    settings.renderDistance = std::clamp(settings.renderDistance, 4, 16);
    settings.fovDeg = std::clamp(settings.fovDeg, 60, 110);
    settings.sensitivity = std::clamp(settings.sensitivity, 0.2f, 3.0f);
    for (const KeyEntry& entry : kKeyEntries) {
        settings.keys.*entry.member =
            std::clamp(settings.keys.*entry.member, 0, GLFW_KEY_LAST);
    }
    return settings;
}

void Settings::save(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::trunc);
    file << std::format("renderDistance {}\nfovDeg {}\nvsync {:d}\nfullscreen {:d}\n"
                        "smoothLighting {:d}\nsensitivity {}\ninvertY {:d}\n",
                        renderDistance, fovDeg, vsync, fullscreen, smoothLighting, sensitivity,
                        invertY);
    for (const KeyEntry& entry : kKeyEntries) {
        file << entry.name << ' ' << keys.*entry.member << '\n';
    }
    for (const std::string& pack : resourcePacks) {
        file << "pack " << pack << '\n';
    }
    if (!file) {
        logError(std::format("failed to write settings to '{}'", path.string()));
    }
}

} // namespace cc
