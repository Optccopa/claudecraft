#include "gl/GlDebug.hpp"

#include "core/Log.hpp"

#include <glad/glad.h>

#include <cassert>
#include <format>

namespace cc::gl {
namespace {

void APIENTRY onDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
                             GLsizei length, const GLchar* message, const void* userParam) {
    (void)source;
    (void)type;
    (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    logError(std::format("GL debug [{}]: {}", id,
                         std::string_view(message, static_cast<std::size_t>(length))));
    assert(severity != GL_DEBUG_SEVERITY_HIGH && "high-severity GL error");
}

} // namespace

void enableDebugOutput() {
    if (!GLAD_GL_KHR_debug || glDebugMessageCallback == nullptr) {
        logInfo("KHR_debug unavailable; GL debug output disabled");
        return;
    }
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(onDebugMessage, nullptr);
    logInfo("GL debug output enabled");
}

} // namespace cc::gl
