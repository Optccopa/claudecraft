#include "app/Settings.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace cc {

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
        } else if (key == "sensitivity") {
            parts >> settings.sensitivity;
        } else if (key == "invertY") {
            parts >> settings.invertY;
        }
    }
    settings.renderDistance = std::clamp(settings.renderDistance, 4, 16);
    settings.fovDeg = std::clamp(settings.fovDeg, 60, 110);
    settings.sensitivity = std::clamp(settings.sensitivity, 0.2f, 3.0f);
    return settings;
}

void Settings::save(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::trunc);
    file << std::format("renderDistance {}\nfovDeg {}\nvsync {:d}\nfullscreen {:d}\n"
                        "sensitivity {}\ninvertY {:d}\n",
                        renderDistance, fovDeg, vsync, fullscreen, sensitivity, invertY);
    if (!file) {
        logError(std::format("failed to write settings to '{}'", path.string()));
    }
}

} // namespace cc
