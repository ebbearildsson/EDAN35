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
std::vector<Tri> createObjectFromFile(const std::string& path,
                                      std::unordered_map<std::string, int>& materialMap);
void rotate_object_y(std::vector<Tri>& tris, float angle);
void rotate_object_x(std::vector<Tri>& tris, float angle);
void rotate_object_z(std::vector<Tri>& tris, float angle);
void scale_object(std::vector<Tri>& tris, float scale);
void translate_object(std::vector<Tri>& tris, glm::vec3 translation);
void add_object(std::vector<Tri>& tris, std::vector<Type>& indices, std::vector<Tri>& triangles, int& ind, int materialIdx = -1);

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