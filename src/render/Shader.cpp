#include "render/Shader.hpp"
#include <filesystem>

Shader::Shader(const std::string vertexShaderPath, const std::string fragmentShaderPath)
{
    std::string vertexShaderSourceStr = readFile(vertexShaderPath);
    std::string fragmentShaderSourceStr = readFile(fragmentShaderPath);
    // Fallback embedded shader sources to avoid runtime file dependency
    static const char *DEFAULT_VERTEX = "#version 330 core\nlayout (location = 0) in vec3 aPos;\nuniform mat4 model;\nuniform mat4 view;\nuniform mat4 projection;\nvoid main() { gl_Position = projection * view * model * vec4(aPos, 1.0); }\n";
    static const char *DEFAULT_FRAGMENT = "#version 330 core\nout vec4 FragColor;\nuniform vec4 wireframeColor;\nvoid main() { FragColor = wireframeColor; }\n";

    if (vertexShaderSourceStr.empty())
    {
        std::cerr << "Shader: using embedded default vertex shader for '" << vertexShaderPath << "'" << std::endl;
        vertexShaderSourceStr = DEFAULT_VERTEX;
    }
    if (fragmentShaderSourceStr.empty())
    {
        std::cerr << "Shader: using embedded default fragment shader for '" << fragmentShaderPath << "'" << std::endl;
        fragmentShaderSourceStr = DEFAULT_FRAGMENT;
    }

#if defined(__EMSCRIPTEN__)
    // WebGL2 / GLSL ES 3.00 compatibility adjustments:
    auto replace_version = [](std::string &s)
    {
        const std::string v330 = "#version 330 core";
        const std::string v300 = "#version 300 es";
        size_t pos = s.find(v330);
        if (pos != std::string::npos)
            s.replace(pos, v330.size(), v300);
    };
    replace_version(vertexShaderSourceStr);
    replace_version(fragmentShaderSourceStr);

    // Ensure fragment shader has a default precision for floats
    if (fragmentShaderSourceStr.find("precision") == std::string::npos)
    {
        // Insert precision line after the version directive if present, otherwise at top
        const std::string v300 = "#version 300 es";
        size_t pos = fragmentShaderSourceStr.find(v300);
        std::string precision = "\nprecision highp float;\n";
        if (pos != std::string::npos)
        {
            size_t insertPos = pos + v300.size();
            fragmentShaderSourceStr.insert(insertPos, precision);
        }
        else
        {
            fragmentShaderSourceStr = precision + fragmentShaderSourceStr;
        }
    }
#endif

    const char *vertexShaderSource = vertexShaderSourceStr.c_str();
    const char *fragmentShaderSource = fragmentShaderSourceStr.c_str();

    ID = compileShaders(vertexShaderSource, fragmentShaderSource);
}

Shader::~Shader()
{
    glDeleteProgram(ID);
}

void Shader::use()
{
    glUseProgram(ID);
}

void Shader::setModelMatrix(const glm::mat4 &model)
{
    glUniformMatrix4fv(glGetUniformLocation(ID, "model"), 1, GL_FALSE, glm::value_ptr(model));
}

void Shader::setViewMatrix(const glm::mat4 &view)
{
    glUniformMatrix4fv(glGetUniformLocation(ID, "view"), 1, GL_FALSE, glm::value_ptr(view));
}

void Shader::setProjectionMatrix(const glm::mat4 &projection)
{
    glUniformMatrix4fv(glGetUniformLocation(ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
}

void Shader::setFragmentColor(glm::vec4 color)
{
    glUniform4f(glGetUniformLocation(ID, "wireframeColor"), color.r, color.g, color.b, color.a);
}

void Shader::setVec3(const char *name, const glm::vec3 &v)
{
    glUniform3f(glGetUniformLocation(ID, name), v.x, v.y, v.z);
}

void Shader::setVec4(const char *name, const glm::vec4 &v)
{
    glUniform4f(glGetUniformLocation(ID, name), v.x, v.y, v.z, v.w);
}

void Shader::setVec4Array(const char *name, const glm::vec4 *v, int count)
{
    if (!v || count <= 0)
        return;
    glUniform4fv(glGetUniformLocation(ID, name), count, glm::value_ptr(v[0]));
}

void Shader::setVec2(const char *name, const glm::vec2 &v)
{
    glUniform2f(glGetUniformLocation(ID, name), v.x, v.y);
}

void Shader::setFloat(const char *name, float v)
{
    glUniform1f(glGetUniformLocation(ID, name), v);
}

void Shader::setInt(const char *name, int v)
{
    glUniform1i(glGetUniformLocation(ID, name), v);
}

void Shader::setMat4(const char *name, const glm::mat4 &m)
{
    glUniformMatrix4fv(glGetUniformLocation(ID, name), 1, GL_FALSE, glm::value_ptr(m));
}

unsigned int Shader::compileShaders(const char *vertexShaderSource, const char *fragmentShaderSource)
{
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
                  << infoLog << std::endl;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
                  << infoLog << std::endl;
    }

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
                  << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

std::string Shader::readFile(const std::string filePath)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    candidates.emplace_back(filePath);
#ifdef ASSET_ROOT
    candidates.emplace_back(fs::path(ASSET_ROOT) / filePath);
#endif
    candidates.emplace_back(fs::path("./") / filePath);
    candidates.emplace_back(fs::path("../") / filePath);
    candidates.emplace_back(fs::path("../src/") / filePath);
    candidates.emplace_back(fs::path("../shaders/") / fs::path(filePath).filename());
    candidates.emplace_back(fs::path("src/shaders/") / fs::path(filePath).filename());
    candidates.emplace_back(fs::path("shaders/") / fs::path(filePath).filename());

    for (auto &c : candidates)
    {
        if (c.empty())
            continue;
        std::ifstream file(c);
        if (file)
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::cerr << "Shader: loaded shader from: " << c << std::endl;
            return buffer.str();
        }
    }

    std::cerr << "Shader: failed to open shader file (tried candidates) : " << filePath << std::endl;
    return std::string();
}