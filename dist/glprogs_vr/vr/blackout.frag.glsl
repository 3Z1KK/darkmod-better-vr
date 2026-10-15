#version 330 core

in vec2 var_TexCoord;
out vec4 FragColor;

uniform float u_alpha;

void main() {
    FragColor = vec4(0, 0, 0, u_alpha);
}
