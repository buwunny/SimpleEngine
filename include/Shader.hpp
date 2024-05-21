#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>

class Shader {
public:
    Shader(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    ~Shader();

    void use();
    void setModelMatrix(const glm::mat4& model);
    void setViewMatrix(const glm::mat4& view);
    void setProjectionMatrix(const glm::mat4& projection);

private:
    unsigned int shaderProgram;
    std::string readShaderSource(const std::string& filePath);
    void compileShaders(const char* vertexShaderSource, const char* fragmentShaderSource);
};;

#endif // SHADER_H