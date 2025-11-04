#version 450 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTex;

void main() {
    FragColor = texture(screenTex, TexCoord);
}
