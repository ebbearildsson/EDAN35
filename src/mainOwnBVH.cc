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

using namespace glm;
using namespace std;

struct Tri { //TODO: compact this
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 c; 
};

struct Node { //TODO: compact this
    vec3 min;
    int idx;
    vec3 max;
    int ownIdx; //TODO: remove
    int left;
    int right;
    int type;
    float _pad1;
};

struct Sphere {
    vec3 center;
    float radius;
};

static_assert(sizeof(Tri) == 64, "Tri size incorrect");
static_assert(sizeof(Node) == 48, "Node size incorrect");
static_assert(sizeof(Sphere) == 16, "Sphere size incorrect");

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

#define N 10
int WIDTH = 800, HEIGHT = 600;
vector<Node> nodes;
vector<Tri> triangles;
vector<Sphere> spheres;

string loadFile(const string& path) {
    ifstream file(path);
    if (!file.is_open()) throw runtime_error("Failed to open file: " + path);
    stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint compileShader(GLenum type, const string& src) {
    GLuint shader = glCreateShader(type);
    const char* csrc = src.c_str();
    glShaderSource(shader, 1, &csrc, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        cerr << "Shader compile error:\n" << info << endl;
    }
    return shader;
}

GLuint createProgram(const string& compPath) {
    string src = loadFile(compPath);
    GLuint shader = compileShader(GL_COMPUTE_SHADER, src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader);
    glLinkProgram(prog);
    glDeleteShader(shader);
    return prog;
}

GLuint createQuadProgram(const string& vertPath, const string& fragPath) {
    string vsrc = loadFile(vertPath);
    string fsrc = loadFile(fragPath);
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

void createCamera(GLuint &cameraUBO, Camera &cam) {
    cam = {
        vec3(0.0f, 0.0f, 10.0f),
        radians(45.0f),
        vec3(0.0f, 0.0f, -1.0f),
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

struct Type {
    int idx;
    int type; // 0 = triangle, 1 = sphere
};

void buildNodeTopDown(int idx, vector<Type> idxs) { //TODO: consider BVH8
    Node node;
    node.idx = -1;
    node.type = -1;
    node.ownIdx = idx;
    node.left = -1;
    node.right = -1;
    nodes.push_back(node);
    if (idxs.size() == 1) {
        Type t = idxs[0];
        if (t.type == 0) {
            Tri& tri = triangles[t.idx];
            nodes[idx].min = min(tri.v0, min(tri.v1, tri.v2));
            nodes[idx].max = max(tri.v0, max(tri.v1, tri.v2));
        } else if (t.type == 1) {
            Sphere& sph = spheres[t.idx];
            nodes[idx].min = sph.center - vec3(sph.radius);
            nodes[idx].max = sph.center + vec3(sph.radius);
        }
        nodes[idx].idx = t.idx;
        nodes[idx].type = t.type;
    } else {
        //! extent calculated before min/max are set, thus axis is always 0
        vec3 extent = nodes[idx].max - nodes[idx].min; 
        int axis = 0;
        if (extent.y > extent.x) axis = 1;
        if (extent.z > extent[axis]) axis = 2;
        
        vector<Type> leftIdxs, rightIdxs;
        sort(idxs.begin(), idxs.end(), [axis](Type a, Type b) { 
            vec3 ac = a.type ? spheres[a.idx].center : triangles[a.idx].c;
            vec3 bc = b.type ? spheres[b.idx].center : triangles[b.idx].c;
            return ac[axis] < bc[axis]; 
        });
        int mid = idxs.size() / 2;
        leftIdxs.insert(leftIdxs.end(), idxs.begin(), idxs.begin() + mid);
        rightIdxs.insert(rightIdxs.end(), idxs.begin() + mid, idxs.end());     

        int leftIdx, rightIdx = -1;
        if (!leftIdxs.empty()) {
            leftIdx = idx + 1;
            nodes[idx].left = leftIdx;
            buildNodeTopDown(leftIdx, leftIdxs);
        } 
        if (!rightIdxs.empty()) {
            rightIdx = nodes.size();
            nodes[idx].right = rightIdx;
            buildNodeTopDown(rightIdx, rightIdxs);  
        }

        if (leftIdx != -1 && rightIdx != -1) {
            nodes[idx].max = max(nodes[leftIdx].max, nodes[rightIdx].max);
            nodes[idx].min = min(nodes[leftIdx].min, nodes[rightIdx].min);
        } else if (leftIdx != -1) {
            nodes[idx].max = nodes[leftIdx].max;
            nodes[idx].min = nodes[leftIdx].min;
        } else if (rightIdx != -1) {
            nodes[idx].max = nodes[rightIdx].max;
            nodes[idx].min = nodes[rightIdx].min;
        } 
    }
}

void tightenBounds(int index) {
    Node* node = &nodes[index];
    if (node->idx != -1) {
        if (node->type == 0) {
            Tri& tri = triangles[node->idx];
            node->min = min(tri.v0, min(tri.v1, tri.v2));
            node->max = max(tri.v0, max(tri.v1, tri.v2));
        } else if (node->type == 1) {
            Sphere& sph = spheres[node->idx];
            node->min = sph.center - vec3(sph.radius);
            node->max = sph.center + vec3(sph.radius);
        }
    } else {
        tightenBounds(node->left);
        tightenBounds(node->right);
        node->min = min(nodes[node->left].min, nodes[node->right].min);
        node->max = max(nodes[node->left].max, nodes[node->right].max);
    }
}

float rnd(float min, float max) {
    return ((float)rand() / RAND_MAX) * (max - min) + min;
}

void init(GLuint triSSBO, GLuint sphSSBO, GLuint bvhSSBO) {
    vector<Type> allIndices;
    for (int i = 0; i < N; i++) {
        Tri tri;
        vec3 j0 = vec3(rnd(-3.0f, 3.0f), rnd(-3.0f, 3.0f), rnd(-3.0f, 3.0f));
        vec3 j1 = vec3(rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f));
        vec3 j2 = vec3(rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f));
        tri.v0 = vec4(j0, 0.0f);
        tri.v1 = vec4(j0 + j1, 0.0f);
        tri.v2 = vec4(j0 + j2, 0.0f);
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        triangles.push_back(tri);
        allIndices.push_back({i, 0});

        cout << "Triangle " << i << ": \n";
        cout << "  v0: (" << tri.v0.x << ", " << tri.v0.y << ", " << tri.v0.z << ")\n";
        cout << "  v1: (" << tri.v1.x << ", " << tri.v1.y << ", " << tri.v1.z << ")\n";
        cout << "  v2: (" << tri.v2.x << ", " << tri.v2.y << ", " << tri.v2.z << ")\n";
    }
    
    glGenBuffers(1, &triSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, triangles.size() * sizeof(Tri), triangles.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triSSBO); // binding = 0 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (int i = 0; i < N; i++) {
        Sphere sph;
        sph.center = vec3(rnd(-3.0f, 3.0f), rnd(-3.0f, 3.0f), rnd(-3.0f, 3.0f));
        sph.radius = rnd(0.1f, 0.5f);
        spheres.push_back(sph);
        allIndices.push_back({i, 1});

        cout << "Sphere " << i << ": \n";
        cout << "  center: (" << sph.center.x << ", " << sph.center.y << ", " << sph.center.z << ")\n";
        cout << "  radius: " << sph.radius << "\n";
    }

    glGenBuffers(1, &sphSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sphSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(Sphere), spheres.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sphSSBO); // binding = 1 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    buildNodeTopDown(0, allIndices);
    tightenBounds(0);

    glGenBuffers(1, &bvhSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodes.size() * sizeof(Node), nodes.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bvhSSBO); // binding = 2 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Raytracer", nullptr, nullptr);
    if (!window) {
        cerr << "Failed to create window\n";
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Failed to init GLAD\n";
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

    float initialTime = glfwGetTime();
    GLuint triSSBO, sphSSBO, bvhSSBO;
    init(triSSBO, sphSSBO, bvhSSBO);
    for (Tri& tri : triangles) {
        tri.c = vec4(rnd(0.1f, 1.0f), rnd(0.1f, 1.0f), rnd(0.1f, 1.0f), 1.0f);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, triangles.size() * sizeof(Tri), triangles.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


    cout << "BVH build time: " << (glfwGetTime() - initialTime) << " seconds\n";
    for (Node& n : nodes) {
        string type = (n.type == 0) ? "⚠️" : (n.type == 1) ? "⭕" : "🌲";
        cout << "Node " << n.ownIdx << ": Index=" << n.idx << ", type=" << type << ", left=" << n.left << ", right=" << n.right << "\n";
        cout << "  Min: (" << n.min.x << ", " << n.min.y << ", " << n.min.z << ")\n";
        cout << "  Max: (" << n.max.x << ", " << n.max.y << ", " << n.max.z << ")\n";
        cout << "  Extent: (" << (n.max.x - n.min.x) << ", " << (n.max.y - n.min.y) << ", " << (n.max.z - n.min.z) << ")\n";
    }

    int nbFrames = 0;
    double lastTime = glfwGetTime();
    double lastFrame = 0.0f;
    int width, height;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
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
        double deltaTime = currentTime - lastFrame;
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

            string title = "Raytracer - " +
                to_string((int)fps) + " FPS | " +
                to_string(frameTimeMs).substr(0, 5) + " ms/frame";
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