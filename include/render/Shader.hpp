#ifndef SHADER_HPP
#define SHADER_HPP

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

class Shader
{
public:
    unsigned int ID;

    Shader(const std::string vertexPath, const std::string fragmentPath);
    ~Shader();

    void use();
    void setModelMatrix(const glm::mat4 &model);
    void setViewMatrix(const glm::mat4 &view);
    void setProjectionMatrix(const glm::mat4 &projection);
    void setFragmentColor(glm::vec4 color);
    void setVec3(const char *name, const glm::vec3 &v);
    void setVec4(const char *name, const glm::vec4 &v);
    // Upload `count` consecutive vec4s to a uniform array. `name` is the array's
    // base name ("uFoo"), not an indexed element.
    void setVec4Array(const char *name, const glm::vec4 *v, int count);
    void setVec2(const char *name, const glm::vec2 &v);
    void setFloat(const char *name, float v);
    void setInt(const char *name, int v);
    void setMat4(const char *name, const glm::mat4 &m);

private:
    std::string readFile(const std::string path);
    unsigned int compileShaders(const char *vertexShaderSource, const char *fragmentShaderSource);
};
#endif // SHADER_HPP