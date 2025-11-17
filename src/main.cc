#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <vector>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using std::vector;

struct Camera {
    vec3 position;
    float fov;
    vec3 forward;
    float aspect;
    vec3 up;
    float _pad;
};

struct Triangle {
    vec3 v0;
    float nx;
    vec3 v1;
    float ny;
    vec3 v2;
    float nz;
};

struct Sphere {
    vec3 center;
    float radius;
};

struct GeoNode {
    vec3 vertex;
    float padOrRadius;
};

struct Mesh {
    vec3 lowerLeftBack;
    int offset;
    vec3 upperRightFront;
    int size;
    vec3 color;
    int type;
    float reflectivity;
    float transperency;
    float emmission;
    float pad;
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

void createLights() {
    Light light = { vec3(0.0f, 2.5f, 2.5f), 1.0f};

    GLuint lightUBO;
    glGenBuffers(1, &lightUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, lightUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Light), &light, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, lightUBO); // binding = 1 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

vector<Triangle> createSphereTriangles(vec3 center, float radius, int longitude_split_count, int latitude_split_count, 
        float emission, float reflectivity, float translucency) {
    vector<Triangle> triangles;
    
    for (int lat = 0; lat < latitude_split_count; ++lat) {
        float theta1 = glm::pi<float>() * lat / latitude_split_count;
        float theta2 = glm::pi<float>() * (lat + 1) / latitude_split_count;

        for (int lon = 0; lon < longitude_split_count; ++lon) {
            float phi1 = 2.0f * glm::pi<float>() * lon / longitude_split_count;
            float phi2 = 2.0f * glm::pi<float>() * (lon + 1) / longitude_split_count;

            vec3 v0 = center + radius * vec3(sin(theta1) * cos(phi1), cos(theta1), sin(theta1) * sin(phi1));
            vec3 v1 = center + radius * vec3(sin(theta2) * cos(phi1), cos(theta2), sin(theta2) * sin(phi1));
            vec3 v2 = center + radius * vec3(sin(theta2) * cos(phi2), cos(theta2), sin(theta2) * sin(phi2));
            vec3 v3 = center + radius * vec3(sin(theta1) * cos(phi2), cos(theta1), sin(theta1) * sin(phi2));

            triangles.push_back({v0, emission, v1, reflectivity, v2, translucency});
            triangles.push_back({v0, emission, v2, reflectivity, v3, translucency});
        }
    }

    return triangles;
}

Mesh getTriangleMesh(vector<Triangle>& triangles, int offset, vec3 color, float reflectivity = 0.0f, float translucency = 0.0f, float emission = 0.0f) {
    vec3 minBound(FLT_MAX);
    vec3 maxBound(-FLT_MAX);
    int count = triangles.size();
    for (int i = 0; i < count; ++i) {
        const Triangle& tri = triangles[i];
        minBound = glm::min(minBound, glm::min(tri.v0, glm::min(tri.v1, tri.v2)));
        maxBound = glm::max(maxBound, glm::max(tri.v0, glm::max(tri.v1, tri.v2)));
    }
    return { minBound, offset, maxBound, count, color, 0, reflectivity, translucency, emission, 0.0f };
}

Mesh getSphereMesh(Sphere& sphere, int offset, vec3 color, float reflectivity = 0.0f, float translucency = 0.0f, float emission = 0.0f) {
    vec3 minBound = sphere.center - vec3(sphere.radius);
    vec3 maxBound = sphere.center + vec3(sphere.radius);
    return { minBound, offset, maxBound, 1, color, 1, reflectivity, translucency, emission, 0.0f };
}

void createTriangles() {
    const float s = 5.0f;
    const float z = 0.0f;
    vector<Triangle> floor = {
        {{-s, z, -s}, 0.0f, { s, z, -s}, 1.0f, { s, z,  s}, 0.0f},
        {{ s, z,  s}, 0.0f, {-s, z,  s}, 1.0f, {-s, z, -s}, 0.0f}
    };
    
    vector<Triangle> ceiling = {
        {{ s, s, -s}, 0.0f, {-s, s,  s}, -1.0f, { s, s,  s}, 0.0f},
        {{ s, s, -s}, 0.0f, {-s, s, -s}, -1.0f, {-s, s,  s}, 0.0f}
    };
    
    vector<Triangle> backWall = {
        {{ s, z, -s}, 0.0f, {-s, z, -s}, 0.0f, {-s, s, -s}, 1.0f},
        {{ s, s, -s}, 0.0f, { s, z, -s}, 0.0f, {-s, s, -s}, 1.0f}
    };
    
    vector<Triangle> rightWall = {
        {{ s, z, -s}, -1.0f, { s, s, -s}, 0.0f, { s, z,  s}, 0.0f},
        {{ s, z,  s}, -1.0f, { s, s, -s}, 0.0f, { s, s,  s}, 0.0f}
    };
    
    vector<Triangle> leftWall = {
        {{-s, z,  s}, 1.0f, {-s, s, -s}, 0.0f, {-s, z, -s}, 0.0f},
        {{-s, z,  s}, 1.0f, {-s, s,  s}, 0.0f, {-s, s, -s}, 0.0f}
    };
    
    vector<Triangle> sphereTriangles = createSphereTriangles(
        vec3(0.0f, 1.0f, 0.0f), 0.5f, 32, 32, 0.0f, 0.5f, 0.0f
    );

    vector<Triangle> triangles;
    triangles.insert(triangles.end(), floor.begin(), floor.end());
    triangles.insert(triangles.end(), ceiling.begin(), ceiling.end());
    triangles.insert(triangles.end(), backWall.begin(), backWall.end());
    triangles.insert(triangles.end(), rightWall.begin(), rightWall.end());
    triangles.insert(triangles.end(), leftWall.begin(), leftWall.end());
    //triangles.insert(triangles.end(), sphereTriangles.begin(), sphereTriangles.end());

    GLuint triangleSSBO, sphereSSBO, meshSSBO, geometrySSBO;
    glGenBuffers(1, &triangleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, triangles.size() * sizeof(Triangle), triangles.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, triangleSSBO); // binding = 1 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Sphere sphere0 = { vec3(1.0f, 1.0f, 0.0f), 0.5f };
    Sphere sphere1 = { vec3(-1.0f, 0.5f, -1.0f), 0.5f };
    vector<Sphere> spheres = { sphere0, sphere1 };

    glGenBuffers(1, &sphereSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sphereSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(Sphere), spheres.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sphereSSBO); // binding = 3 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    vector<GeoNode> geometry;
    for (const auto& tri : triangles) {
        geometry.push_back({ tri.v0, tri.nx });
        geometry.push_back({ tri.v1, tri.ny });
        geometry.push_back({ tri.v2, tri.nz });
    }
    for (const auto& sph : spheres) {
        geometry.push_back({ sph.center, sph.radius });
    }

    glGenBuffers(1, &geometrySSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, geometrySSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, geometry.size() * sizeof(GeoNode), geometry.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, geometrySSBO); // binding = 4 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


    Mesh floorMesh = getTriangleMesh(floor, 0, vec3(1.0f, 1.0f, 1.0f));
    Mesh ceilingMesh = getTriangleMesh(ceiling, floorMesh.offset + floorMesh.size, vec3(1.0f, 1.0f, 1.0f));
    Mesh backWallMesh = getTriangleMesh(backWall, ceilingMesh.offset + ceilingMesh.size, vec3(1.0f, 1.0f, 1.0f), 1.0f);
    Mesh rightWallMesh = getTriangleMesh(rightWall, backWallMesh.offset + backWallMesh.size, vec3(0.2f, 1.0f, 0.2f), 0.5f);
    Mesh leftWallMesh = getTriangleMesh(leftWall, rightWallMesh.offset + rightWallMesh.size, vec3(0.2f, 0.2f, 1.0f), 0.5f);
    //Mesh sphereMesh = getTriangleMesh(sphereTriangles, leftWallMesh.offset + leftWallMesh.size, vec3(1.0f, 0.2f, 0.2f));
    Mesh realSphereMesh = getSphereMesh(sphere0, 0, vec3(1.0f, 0.2f, 0.2f), 0.8f, 0.0f, 0.0f);
    Mesh realSphereMesh2 = getSphereMesh(sphere1, 1, vec3(1.0f, 1.0f, 0.2f), 0.3f, 0.0f, 0.0f);

    vector<Mesh> meshes = {
        floorMesh,
        ceilingMesh,
        backWallMesh,
        rightWallMesh,
        leftWallMesh,
        //sphereMesh,
        realSphereMesh,
        realSphereMesh2
    };

    glGenBuffers(1, &meshSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, meshSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, meshes.size() * sizeof(Mesh), meshes.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, meshSSBO); // binding = 2 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void createCamera(GLuint &cameraUBO, Camera &cam) {
    cam = {
        vec3(0.0f, 3.5f, 10.0f),
        glm::radians(45.0f),
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
    glfwSwapInterval(0);

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

    GLuint cameraUBO;
    Camera cam;
    createCamera(cameraUBO, cam);
    createTriangles();
    createLights();

    vec2 mousePos = vec2(0.0f);
    GLuint mouseUBO;
    glGenBuffers(1, &mouseUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, mouseUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(vec2), &mousePos, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, mouseUBO); // binding = 2 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    int nbFrames = 0;
    double lastTime = glfwGetTime();
    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

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

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        if (xpos != mousePos.x || ypos != mousePos.y) {
            mousePos = vec2(xpos, ypos);
            glBindBuffer(GL_UNIFORM_BUFFER, mouseUBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(vec2), &mousePos);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

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