#include "gl/Shader.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace cc::gl {
namespace {

[[nodiscard]] std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("cannot open shader file '{}'", path));
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

[[nodiscard]] GLuint compileStage(GLenum type, const std::string& source, const std::string& path) {
    const GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::string log(1024, '\0');
        GLsizei length = 0;
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
        log.resize(static_cast<std::size_t>(length));
        glDeleteShader(shader);
        throw std::runtime_error(std::format("shader compile failed ({}):\n{}", path, log));
    }
    return shader;
}

} // namespace

ShaderProgram ShaderProgram::fromFiles(const std::string& vertPath, const std::string& fragPath) {
    const GLuint vert = compileStage(GL_VERTEX_SHADER, readFile(vertPath), vertPath);
    const GLuint frag = compileStage(GL_FRAGMENT_SHADER, readFile(fragPath), fragPath);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::string log(1024, '\0');
        GLsizei length = 0;
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
        log.resize(static_cast<std::size_t>(length));
        glDeleteProgram(program);
        throw std::runtime_error(
            std::format("program link failed ({} + {}):\n{}", vertPath, fragPath, log));
    }
    return ShaderProgram{program};
}

ShaderProgram::~ShaderProgram() {
    glDeleteProgram(m_id);
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : m_id{std::exchange(other.m_id, 0)}, m_locations{std::move(other.m_locations)} {}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        glDeleteProgram(m_id);
        m_id = std::exchange(other.m_id, 0);
        m_locations = std::move(other.m_locations);
    }
    return *this;
}

void ShaderProgram::use() const noexcept {
    glUseProgram(m_id);
}

GLint ShaderProgram::location(const char* name) {
    if (const auto it = m_locations.find(name); it != m_locations.end()) {
        return it->second;
    }
    const GLint loc = glGetUniformLocation(m_id, name);
    m_locations.emplace(name, loc);
    return loc;
}

void ShaderProgram::setMat4(const char* name, const glm::mat4& value) {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderProgram::setVec2(const char* name, const glm::vec2& value) {
    glUniform2fv(location(name), 1, glm::value_ptr(value));
}

void ShaderProgram::setVec3(const char* name, const glm::vec3& value) {
    glUniform3fv(location(name), 1, glm::value_ptr(value));
}

void ShaderProgram::setVec4(const char* name, const glm::vec4& value) {
    glUniform4fv(location(name), 1, glm::value_ptr(value));
}

void ShaderProgram::setFloat(const char* name, float value) {
    glUniform1f(location(name), value);
}

void ShaderProgram::setInt(const char* name, int value) {
    glUniform1i(location(name), value);
}

} // namespace cc::gl
