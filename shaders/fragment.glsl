#version 330 core
out vec4 FragColor;
uniform vec4 wireframeColor;
void main() {
    FragColor = wireframeColor;
}