#ifndef SHADER_HPP
#define SHADER_HPP

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

class Shader {
public:
    unsigned int ID; 

    Shader(const std::string vertexPath, const std::string fragmentPath);
    ~Shader();
    
    void use();
    void setModelMatrix(const glm::mat4& model);
    void setViewMatrix(const glm::mat4& view);
    void setProjectionMatrix(const glm::mat4& projection);
private:
    std::string readFile(const std::string path);
    unsigned int compileShaders(const char* vertexShaderSource, const char* fragmentShaderSource);

};
#endif // SHADER_HPP