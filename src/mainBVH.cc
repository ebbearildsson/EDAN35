#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <vector>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using std::vector;

struct Tri { vec3 vertex0, vertex1, vertex2; vec3 centroid; };

#define N 64
Tri tri[N];
uint triIdx[N];

struct Camera {
    vec3 position;
    float fov;
    vec3 forward;
    float aspect;
    vec3 up;
    float _pad;
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

int WIDTH = 800, HEIGHT = 600;

void createCamera(GLuint &cameraUBO, Camera &cam) {
    cam = {
        vec3(0.0f, 0.0f, 10.0f),
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

struct BVHNode {
    vec3 aabbMin, aabbMax;
    uint leftFirst, triCount;
    bool isLeaf() { return triCount > 0; }
};
BVHNode bvhNode[N * 2 - 1];
uint rootNodeIdx = 0, nodesUsed = 1;

void UpdateNodeBounds( uint nodeIdx ) {
    BVHNode& node = bvhNode[nodeIdx];
    node.aabbMin = vec3( 1e30f );
    node.aabbMax = vec3( -1e30f );
    for (uint first = node.leftFirst, i = 0; i < node.triCount; i++) {
        uint leafTriIdx = triIdx[first + i];
        Tri& leafTri = tri[leafTriIdx];
        node.aabbMin = glm::min( node.aabbMin, leafTri.vertex0 ),
        node.aabbMin = glm::min( node.aabbMin, leafTri.vertex1 ),
        node.aabbMin = glm::min( node.aabbMin, leafTri.vertex2 ),
        node.aabbMax = glm::max( node.aabbMax, leafTri.vertex0 ),
        node.aabbMax = glm::max( node.aabbMax, leafTri.vertex1 ),
        node.aabbMax = glm::max( node.aabbMax, leafTri.vertex2 );
    }
}

void swap( uint& a, uint& b ) {
    uint temp = a;
    a = b;
    b = temp;
}

void Subdivide( uint nodeIdx ) {
    BVHNode& node = bvhNode[nodeIdx];
    if (node.triCount <= 2) return;
    vec3 extent = node.aabbMax - node.aabbMin;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    float splitPos = node.aabbMin[axis] + extent[axis] * 0.5f;
    int i = node.leftFirst;
    int j = i + node.triCount - 1;
    while (i <= j) {
        if (tri[triIdx[i]].centroid[axis] < splitPos) i++;
        else swap( triIdx[i], triIdx[j--] );
    }
    int leftCount = i - node.leftFirst;
    if (leftCount == 0 || leftCount == node.triCount) return;
    int leftChildIdx = nodesUsed++;
    int rightChildIdx = nodesUsed++;
    bvhNode[leftChildIdx].leftFirst = node.leftFirst;
    bvhNode[leftChildIdx].triCount = leftCount;
    bvhNode[rightChildIdx].leftFirst = i;
    bvhNode[rightChildIdx].triCount = node.triCount - leftCount;
    node.leftFirst = leftChildIdx;
    node.triCount = 0;
    UpdateNodeBounds( leftChildIdx );
    UpdateNodeBounds( rightChildIdx );
    Subdivide( leftChildIdx );
    Subdivide( rightChildIdx );
}

void BuildBVH() {
    for (int i = 0; i < N; i++) {
        triIdx[i] = i;
        tri[i].centroid = (tri[i].vertex0 + tri[i].vertex1 + tri[i].vertex2) * 0.3333f;
    }
    BVHNode& root = bvhNode[rootNodeIdx];
    root.leftFirst = 0;
    root.triCount = 0;
    UpdateNodeBounds( rootNodeIdx );
    Subdivide( rootNodeIdx );
}

void Init() {
    for (int i = 0; i < N; i++) {
        vec3 r0 = vec3(rand(), rand(), rand()) / (float)RAND_MAX;
        vec3 r1 = vec3(rand(), rand(), rand()) / (float)RAND_MAX;
        vec3 r2 = vec3(rand(), rand(), rand()) / (float)RAND_MAX;
        tri[i].vertex0 = r0 * 9.0f - vec3(5.0f);
        tri[i].vertex1 = tri[i].vertex0 + r1;
        tri[i].vertex2 = tri[i].vertex0 + r2;
    }

    GLuint triSSBO;
    glGenBuffers(1, &triSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, N * sizeof(Tri), tri, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triSSBO); // binding = 0 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    BuildBVH();

    GLuint bvhSSBO;
    glGenBuffers(1, &bvhSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodesUsed * sizeof(BVHNode), bvhNode, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhSSBO); // binding = 1 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    GLuint triIdxSSBO;
    glGenBuffers(1, &triIdxSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triIdxSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, N * sizeof(uint), triIdx, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, triIdxSSBO); // binding = 2 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

int main() {
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

    GLuint computeProgram = createProgram("../shaders/traceBVH.glsl");

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
    createLights();

    vec2 mousePos = vec2(0.0f);
    GLuint mouseUBO;
    glGenBuffers(1, &mouseUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, mouseUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(vec2), &mousePos, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, mouseUBO); // binding = 2 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    Init();

    int nbFrames = 0;
    double lastTime = glfwGetTime();
    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width != WIDTH || height != HEIGHT) {
            WIDTH = width;
            HEIGHT = height;
            glViewport(0, 0, width, height);
            cam.aspect = float(WIDTH) / float(HEIGHT);
            glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Camera), &cam);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

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