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
#include <unordered_map>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using std::vector;

#define N 64

struct Tri { vec3 v0, v1, v2, c; };
Tri tri[N];

struct Node {
    vec3 aabbMin;
    int triangleIndex;
    vec3 aabbMax;
    float _pad1;
    Node* left;
    Node* right;
};

struct GPUNode {
    vec3 aabbMin;
    int triangleIndex;
    vec3 aabbMax;
    float _pad1;
    int leftIdx;
    int rightIdx;
};

std::vector<GPUNode> getPreorderTraversal(Node* root) {
    std::vector<GPUNode> result;
    if (!root) return result;

    std::vector<Node*> order;
    order.reserve(256);
    std::vector<Node*> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        order.push_back(current);
        if (current->right) stack.push_back(current->right);
        if (current->left)  stack.push_back(current->left);
    }

    std::unordered_map<Node*, int> indexMap;
    indexMap.reserve(order.size() * 2);
    for (size_t i = 0; i < order.size(); ++i) indexMap[order[i]] = (int)i;

    result.reserve(order.size());
    for (Node* n : order) {
        int leftIdx  = n->left  ? indexMap[n->left]  : -1;
        int rightIdx = n->right ? indexMap[n->right] : -1;
        result.push_back({
            n->aabbMin,
            n->triangleIndex,
            n->aabbMax,
            n->_pad1,
            leftIdx,
            rightIdx
        });
    }

    return result;
}

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
        (float)WIDTH / (float)HEIGHT,
        vec3(0.0f, 1.0f, 0.0f),
        0.0f
    };
    glGenBuffers(1, &cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Camera), &cam, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, cameraUBO); // binding = 0 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

Node* getNode(int start, int end) {
    Node* node = nullptr;
    if (start >= end) return nullptr;
    if (end - start == 1) {
        node = new Node();
        node->aabbMin = glm::min(tri[start].v0, glm::min(tri[start].v1, tri[start].v2));
        node->aabbMax = glm::max(tri[start].v0, glm::max(tri[start].v1, tri[start].v2));
        node->triangleIndex = start;
        node->left = nullptr;
        node->right = nullptr;
        return node;
    } else {
        int mid = start + (end - start) / 2;
        Node* leftNode = getNode(start, mid);
        Node* rightNode = getNode(mid, end);
        node = new Node();
        if (leftNode && rightNode) {
            node->aabbMin = glm::min(leftNode->aabbMin, rightNode->aabbMin);
            node->aabbMax = glm::max(leftNode->aabbMax, rightNode->aabbMax);
        } else if (leftNode) {
            node->aabbMin = leftNode->aabbMin;
            node->aabbMax = leftNode->aabbMax;
        } else if (rightNode) {
            node->aabbMin = rightNode->aabbMin;
            node->aabbMax = rightNode->aabbMax;
        } else {
            node->aabbMin = vec3(FLT_MAX);
            node->aabbMax = vec3(-FLT_MAX);
        }
        node->triangleIndex = -1;
        node->left = leftNode;
        node->right = rightNode;
    }

    return node;
}

Node* BuildBVH() {
    std::sort(tri, tri + N, [](const Tri& a, const Tri& b) {
        return a.c.x < b.c.x;
    });
    
    Node* root = getNode(0, N);
    return root;
}

void Init() {
    for (int i = 0; i < N; i++) {
        vec3 r0 = vec3(rand(), rand(), rand()) / (float)RAND_MAX;
        vec3 r1 = vec3(rand()) / (float)RAND_MAX;
        vec3 r2 = vec3(rand()) / (float)RAND_MAX;
        tri[i].v0 = r0 * 9.0f - vec3(5.0f);
        tri[i].v1 = tri[i].v0 + r1;
        tri[i].v2 = tri[i].v0 + r2;
        tri[i].c = (tri[i].v0 + tri[i].v1 + tri[i].v2) / 3.0f;
    }

    GLuint triSSBO;
    glGenBuffers(1, &triSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, N * sizeof(Tri), tri, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triSSBO); // binding = 0 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Node* root = BuildBVH();
    std::vector<GPUNode> bvhNodes = getPreorderTraversal(root);
    GLuint bvhSSBO;
    glGenBuffers(1, &bvhSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bvhNodes.size() * sizeof(GPUNode), bvhNodes.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhSSBO); // binding = 1 for SSBO
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

    GLuint computeProgram = createProgram("../shaders/traceOwnBVH.glsl");

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