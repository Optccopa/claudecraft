#pragma once

#include <glad/glad.h>
#include <glm/fwd.hpp>

#include <string>
#include <unordered_map>

namespace cc::gl {

class ShaderProgram {
public:
    // Throws std::runtime_error with the GL info log on compile/link failure.
    [[nodiscard]] static ShaderProgram fromFiles(const std::string& vertPath,
                                                 const std::string& fragPath);

    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    void use() const noexcept;
    void setMat4(const char* name, const glm::mat4& value);
    void setVec2(const char* name, const glm::vec2& value);
    void setVec3(const char* name, const glm::vec3& value);
    void setVec4(const char* name, const glm::vec4& value);
    void setFloat(const char* name, float value);
    void setInt(const char* name, int value);

private:
    explicit ShaderProgram(GLuint id) noexcept : m_id{id} {}
    [[nodiscard]] GLint location(const char* name);

    GLuint m_id = 0;
    std::unordered_map<std::string, GLint> m_locations;
};

} // namespace cc::gl
