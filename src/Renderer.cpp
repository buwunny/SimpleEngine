#include "../include/Renderer.hpp"


Renderer::Renderer(Window& window, Shader& shader, Camera& camera) : window(window), shader(shader), camera(camera) {
    // Set up vertex data and buffers and configure vertex attributes
    
    // Calculate the number of vertices and indices for a 10x10 grid
    const int gridSize = 100;
    const int numVertices = gridSize * gridSize * 4;
    const int numIndices = gridSize * gridSize * 8;

    // Create the vertices and indices for the grid
    float vertices[numVertices];
    unsigned int indices[numIndices];

    float stepSize = 5.0f;
    int vertexIndex = 0;
    int indexIndex = 0;

    for (int i = 0; i < gridSize; i++) {
        for (int j = 0; j < gridSize; j++) {
            float x = j * stepSize - 0.5f;
            float z = i * stepSize - 0.5f; // Change y to z

            vertices[vertexIndex++] = x;
            vertices[vertexIndex++] = 0.0f; // Set y to 0.0f
            vertices[vertexIndex++] = -z; // Set z to the negative calculated value for moving backwards

            if (j < gridSize - 1) {
                indices[indexIndex++] = i * gridSize + j;
                indices[indexIndex++] = i * gridSize + j + 1;
            }

            if (i < gridSize - 1) {
                indices[indexIndex++] = i * gridSize + j;
                indices[indexIndex++] = (i + 1) * gridSize + j;
            }
        }
    }
    
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

Renderer::~Renderer() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}

void Renderer::render() {
    // Clear the screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);//(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Use the shader program
    shader.use();

    // Create transformations
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    // model = glm::rotate(model, (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));
    view = camera.getViewMatrix();//glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));
    projection = camera.getProjectionMatrix();//glm::perspective(glm::radians(45.0f), (float)window.getWidth() / (float)window.getHeight(), 0.1f, 100.0f);

    // Get matrix's uniform location and set matrix
    shader.setModelMatrix(model);
    shader.setViewMatrix(view);
    shader.setProjectionMatrix(projection);

    // Render the cube
    glBindVertexArray(VAO);
    glDrawElements(GL_LINES, 300000, GL_UNSIGNED_INT, 0);

}