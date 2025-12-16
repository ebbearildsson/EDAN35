#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <structs.hh>

// Random helpers
float rnd(float min, float max);
int   rnd(int min, int max);

// String helpers
std::vector<std::string> split(const std::string& s, const std::string& delimiter);

// OBJ loader
std::vector<Mesh> createObjectFromFile(const std::string& path);
void add_object(std::vector<Tri>& tris, int materialIdx = -1);
void apply_transform(Tri& tri, const glm::mat4& transform);
void transform(const std::vector<Mesh>& meshes, const glm::mat4& transform);
mat4 get_translation(glm::vec3 translation);
mat4 get_scaling(float scale);
mat4 get_rotation_x(float angle);
mat4 get_rotation_y(float angle);
mat4 get_rotation_z(float angle);

// File IO
std::string loadFile(const std::string& path);

// OpenGL shader/program helpers
GLuint compileShader(GLenum type, const std::string& src);
GLuint createProgram(const std::string& compPath);
GLuint createQuadProgram(const std::string& vertPath, const std::string& fragPath);
template <typename T>
GLuint createAndFillSSBO(GLuint ssbo, int binding, const std::vector<T>& data) {
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(T)), data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return ssbo;
}

template <typename T>
GLuint createAndFillUBO(GLuint& ubo, int binding, const T& data) {
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(T), &data, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return ubo;
}

template <typename T>
GLuint updateUBO(GLuint& ubo, const T& data) {
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(T), &data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return ubo;
}

// Input handling
bool processInput(GLFWwindow* window, Camera* cam, float deltaTime);

// UBO creation
void createLights();
void createCamera(GLuint& cameraUBO, Camera& cam, int width, int height);