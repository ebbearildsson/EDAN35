#version 450 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTex;

void main() {
    vec4 color = texture(screenTex, TexCoord);
    color.rgb /= color.a;
    FragColor = color;
}
