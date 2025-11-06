#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <vector>

using namespace glm;
struct Camera {
    vec3 position;
    float fov;
    vec3 forward;
    float aspect;
    vec3 up;
    float _pad;
};

struct Sphere {
    vec3 center;
    float radius;
    vec4 color;
    float emission;
    float reflectivity;
    float translucency;
    float refractiveIndex;
};

struct Triangle {
    vec3 v0;
    float _pad0;
    vec3 v1;
    float _pad1;
    vec3 v2;
    float _pad2;
    vec4 color;
};

struct Light {
    vec3 direction;
    float intensity;
};

std::string loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Failed to open file: " + path);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint compileShader(GLenum type, const std::string& src) {
    GLuint shader = glCreateShader(type);
    const char* csrc = src.c_str();
    glShaderSource(shader, 1, &csrc, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        std::cerr << "Shader compile error:\n" << info << std::endl;
    }
    return shader;
}

GLuint createProgram(const std::string& compPath) {
    std::string src = loadFile(compPath);
    GLuint shader = compileShader(GL_COMPUTE_SHADER, src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader);
    glLinkProgram(prog);
    glDeleteShader(shader);
    return prog;
}

GLuint createQuadProgram(const std::string& vertPath, const std::string& fragPath) {
    std::string vsrc = loadFile(vertPath);
    std::string fsrc = loadFile(fragPath);
    GLuint vsh = compileShader(GL_VERTEX_SHADER, vsrc);
    GLuint fsh = compileShader(GL_FRAGMENT_SHADER, fsrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vsh);
    glAttachShader(prog, fsh);
    glLinkProgram(prog);
    glDeleteShader(vsh);
    glDeleteShader(fsh);
    return prog;
}

bool processInput(GLFWwindow* window, Camera* cam, float deltaTime) {
    float cameraSpeed = 2.5f;
    float velocity = cameraSpeed * deltaTime;
    bool moved = false;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        cam->position += cam->forward * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        cam->position -= cam->forward * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        cam->position -= normalize(cross(cam->forward, cam->up)) * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        cam->position += normalize(cross(cam->forward, cam->up)) * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        cam->position += cam->up * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        cam->position -= cam->up * velocity;
        moved = true;
    }

    return moved;
}

void createLights(GLuint &lightUBO) {
    Light light = {
        normalize(vec3(50.0f, 50.0f, 50.0f)),
        1.0f};
    glGenBuffers(1, &lightUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, lightUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Light), &light, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, lightUBO); // binding = 1 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void createTriangles(GLuint &triangleSSBO) {
    const float s = 5.0f;
    const float z = 0.0f;
    std::vector<Triangle> triangles = {
        {{-s, z, -s}, 0.0f, { s, z, -s}, 0.0f, { s, z,  s}, 0.0f, {1.0f, 1.0f, 1.0f, 1.0f}}, // Floor
        {{-s, z,  s}, 0.0f, {-s, z, -s}, 0.0f, { s, z,  s}, 0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{-s, s,  s}, 0.0f, { s, s,  s}, 0.0f, { s, s, -s}, 0.0f, {1.0f, 1.0f, 1.0f, 1.0f}}, // Ceiling
        {{-s, s,  s}, 0.0f, { s, s, -s}, 0.0f, {-s, s, -s}, 0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{-s, z, -s}, 0.0f, {-s, s, -s}, 0.0f, { s, z, -s}, 0.0f, {1.0f, 1.0f, 1.0f, 1.0f}}, // Back wall
        {{ s, z, -s}, 0.0f, {-s, s, -s}, 0.0f, { s, s, -s}, 0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{ s, z, -s}, 0.0f, { s, s, -s}, 0.0f, { s, z,  s}, 0.0f, {1.0f, 0.2f, 1.0f, 1.0f}}, // Right wall
        {{ s, z,  s}, 0.0f, { s, s, -s}, 0.0f, { s, s,  s}, 0.0f, {1.0f, 0.2f, 1.0f, 1.0f}},
        {{-s, z,  s}, 0.0f, {-s, s, -s}, 0.0f, {-s, z, -s}, 0.0f, {0.2f, 1.0f, 0.2f, 1.0f}}, // Left wall
        {{-s, z,  s}, 0.0f, {-s, s,  s}, 0.0f, {-s, s, -s}, 0.0f, {0.2f, 1.0f, 0.2f, 1.0f}},
    
        // Ceiling light (two triangles)
        {{-1.0f, s - 0.01f, -1.0f}, 0.0f, {  1.0f, s - 0.01f,  1.0f}, 0.0f, { 1.0f, s - 0.01f, -1.0f}, 1.0f, {1.0f, 0.0f, 1.0f, 1.0f}},
        {{-1.0f, s - 0.01f, -1.0f}, 0.0f, { -1.0f, s - 0.01f,  1.0f}, 0.0f, { 1.0f, s - 0.01f,  1.0f}, 1.0f, {1.0f, 0.0f, 1.0f, 1.0f}}
    
    };
    glGenBuffers(1, &triangleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, triangles.size() * sizeof(Triangle), triangles.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, triangleSSBO); // binding = 1 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void createSpheres(GLuint &sphereSSBO) {
    std::vector<Sphere> spheres = {
        {{0.0f, 1.0f, 0.0f}, 0.5f, {1.0f, 0.2f, 0.2f, 1.0f}, 0.0f, 0.5f, 0.0f, 1.5f},
        {{-1.5f, 1.0f, -1.0f}, 0.5f, {0.2f, 0.2f, 1.0f, 1.0f}, 0.0f, 0.0f, 0.8f, 1.3f},
        {{1.5f, 1.0f, -1.0f}, 0.5f, {0.2f, 1.0f, 0.2f, 1.0f}, 0.5f, 0.0f, 0.0f, 1.0f},
        {{0.0f, 3.0f, 0.0f}, 0.3f, {1.0f, 1.0f, 1.0f, 1.0f}, 5.0f, 0.0f, 0.0f, 1.0f},
    };

    glGenBuffers(1, &sphereSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sphereSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(Sphere), spheres.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sphereSSBO); // binding = 0 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void createCamera(GLuint &cameraUBO, Camera &cam) {
    cam = {
        vec3(0.0f, 1.5f, 5.0f),
        radians(45.0f),
        vec3(0.0f, -0.2f, -1.0f),
        800.0f / 600.0f,
        vec3(0.0f, 1.0f, 0.0f),
        0.0f
    };
    glGenBuffers(1, &cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Camera), &cam, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, cameraUBO); // binding = 0 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

int main() {
    const int WIDTH = 800, HEIGHT = 600;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Raytracer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, WIDTH, HEIGHT);
    glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    GLuint computeProgram = createProgram("../shaders/trace.glsl");

    float quadVertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    GLuint quadProgram = createQuadProgram("../shaders/quad.vert", "../shaders/quad.frag");

    GLuint sphereSSBO, triangleSSBO, cameraUBO, lightUBO;

    createSpheres(sphereSSBO);

    createTriangles(triangleSSBO);

    Camera cam;
    createCamera(cameraUBO, cam);

    createLights(lightUBO);

    int nbFrames = 0;
    double lastTime = glfwGetTime();
    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glUseProgram(computeProgram);
        glDispatchCompute((GLuint)ceil(WIDTH / 8.0f), (GLuint)ceil(HEIGHT / 8.0f), 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(quadProgram);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        nbFrames++;
        double currentTime = glfwGetTime();
        float deltaTime = currentTime - lastFrame;
        lastFrame = currentTime;

        if(processInput(window, &cam, deltaTime)) {
            glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Camera), &cam);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

        if (currentTime - lastTime >= 1.0) {
            double fps = double(nbFrames) / (currentTime - lastTime);
            double frameTimeMs = 1000.0 / fps;

            std::string title = "Raytracer - " +
                std::to_string((int)fps) + " FPS | " +
                std::to_string(frameTimeMs).substr(0, 5) + " ms/frame";
            glfwSetWindowTitle(window, title.c_str());

            nbFrames = 0;
            lastTime = currentTime;
        }   
        glfwSwapBuffers(window);
    }

    glDeleteProgram(computeProgram);
    glDeleteProgram(quadProgram);
    glDeleteTextures(1, &tex);
    glfwTerminate();
    return 0;
}