#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <vector>
#include <unordered_map>

using namespace glm;
using namespace std;

#define N 10
#define DEBUG 0
int WIDTH = 600, HEIGHT = 600;

struct GPUTri { //TODO: compact this
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 n;
};

struct Tri {
    vec3 v0;
    vec3 v1;
    vec3 v2;
    vec3 c;
    vec3 normal;
    int materialIdx;
};

struct GPUSph {
    vec3 center;
    float radius;
};

struct Sph {
    vec3 center;
    float radius;
    int materialIdx;
};

struct Material { //TODO: compact this
    vec4 color;
    float reflectivity;
    float translucency;
    float emission;
    float refractiveIndex;
};

struct Node { //TODO: compact this
    vec3 min;
    int idx;
    vec3 max;
    int ownIdx; //TODO: remove
    int left;
    int right;
    int type;
    int materialIdx;
};

static_assert(sizeof(GPUTri) == 64, "GPUTri size incorrect");
static_assert(sizeof(Node) == 48, "Node size incorrect");
static_assert(sizeof(GPUSph) == 16, "GPUSph size incorrect");

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

vector<Node> nodes;
vector<Tri> triangles;
vector<Sph> spheres;
vector<Material> materials;
std::unordered_map<string, int> materialMap;

float rnd(float min, float max) {
    return ((float)rand() / RAND_MAX) * (max - min) + min;
}

int rnd(int min, int max) {
    return rand() % (max - min) + min;
}

vector<string> split(const string& s, const string& delimiter) {
    vector<string> tokens;
    size_t pos = 0;
    string token;
    string str = s;
    while ((pos = str.find(delimiter)) != string::npos) {
        token = str.substr(0, pos);
        tokens.push_back(token);
        str.erase(0, pos + delimiter.length());
    }
    tokens.push_back(str);

    return tokens;
}

vector<Tri> createObjectFromFile(const string& path) {
    vector<vec3> temp_vertices;
    vector<vec3> temp_normals;
    vector<Tri> tris;
    ifstream file(path);
    if (!file.is_open()) {
        throw runtime_error("Failed to open OBJ file: " + path);
    }
    string line;
    int currentMaterial = -1;
    while (getline(file, line)) {
        if (line.substr(0, 6) == "usemtl") {
            istringstream s(line.substr(6));
            string materialName;
            s >> materialName;
            if (materialMap.find(materialName) != materialMap.end()) {
                currentMaterial = materialMap[materialName];
            } else {
                currentMaterial = -1;
            }
        } else if (line.substr(0, 2) == "v ") {
            istringstream s(line.substr(2));
            vec3 v;
            s >> v.x; s >> v.y; s >> v.z;
            temp_vertices.push_back(v);
        } else if (line.substr(0, 2) == "vn") {
            istringstream s(line.substr(2));
            vec3 n;
            s >> n.x; s >> n.y; s >> n.z;
            temp_normals.push_back(n);
        } else if (line.substr(0, 2) == "f ") {
            istringstream s(line.substr(2));
            int vIndex[3];
            int nIndex[3];
            char slash;
            
            string values = line.substr(2);
            vector<string> parts = split(values, " ");
            for (auto& part : parts) {
                vector<string> indices = split(part, "/");
                if (indices.size() == 3) {
                    vIndex[&part - &parts[0]] = stoi(indices[0]) - 1;
                    nIndex[&part - &parts[0]] = stoi(indices[2]) - 1;
                } else {
                    vIndex[&part - &parts[0]] = stoi(indices[0]) - 1;
                    nIndex[&part - &parts[0]] = -1;
                }
            }

            Tri tri;
            tri.v0 = vec3(temp_vertices[vIndex[0]]);
            tri.v1 = vec3(temp_vertices[vIndex[1]]);
            tri.v2 = vec3(temp_vertices[vIndex[2]]);
            tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
            if (nIndex[0] != -1 && nIndex[1] != -1 && nIndex[2] != -1) {
                vec3 n0 = temp_normals[nIndex[0]];
                vec3 n1 = temp_normals[nIndex[1]];
                vec3 n2 = temp_normals[nIndex[2]];
                tri.normal = normalize((n0 + n1 + n2) / 3.0f);
            } else {
                tri.normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
            }
            tri.materialIdx = currentMaterial;
            tris.push_back(tri);
        }
    }

    file.close();
    return tris;
}

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
    } else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        cam->position -= cam->forward * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        cam->position -= normalize(cross(cam->forward, cam->up)) * velocity;
        moved = true;
    } else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        cam->position += normalize(cross(cam->forward, cam->up)) * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        cam->position += cam->up * velocity;
        moved = true;
    } else if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        cam->position -= cam->up * velocity;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        cam->forward += normalize(cross(cam->up, cam->forward)) * velocity;
        moved = true;
    } else if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        cam->forward -= normalize(cross(cam->up, cam->forward)) * velocity;
        moved = true;
    }

    return moved;
}

void createLights() {
    Light light = { vec3(0.0f, 3.5f, 3.5f), 1.0f};

    GLuint lightUBO;
    glGenBuffers(1, &lightUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, lightUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Light), &light, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, lightUBO); // binding = 1 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void createCamera(GLuint &cameraUBO, Camera &cam) {
    cam = {
        vec3(0.0f, 0.0f, 20.0f),
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

void buildNode(int idx, vec3 minv, vec3 maxv, vector<Type> idxs) {
    Node node;
    node.idx = -1;
    node.type = -1;
    node.ownIdx = idx;
    node.left = -1;
    node.right = -1;
    node.min = minv;
    node.max = maxv;
    nodes.push_back(node);
    if (idxs.size() == 1) {
        Type t = idxs[0];
        if (t.type == 0) {
            Tri& tri = triangles[t.idx];
            nodes[idx].min = min(tri.v0, min(tri.v1, tri.v2));
            nodes[idx].max = max(tri.v0, max(tri.v1, tri.v2));
        } else if (t.type == 1) {
            Sph& sph = spheres[t.idx];
            nodes[idx].min = sph.center - vec3(sph.radius);
            nodes[idx].max = sph.center + vec3(sph.radius);
        }
        nodes[idx].idx = t.idx;
        nodes[idx].type = t.type;
        if (t.type == 0) {
            int material = triangles[t.idx].materialIdx;
            if (material >= 0) nodes[idx].materialIdx = material;
            else nodes[idx].materialIdx = 0;
        } 
        if (t.type == 1) {
            int material = spheres[t.idx].materialIdx;
            if (material >= 0) nodes[idx].materialIdx = material;
            else nodes[idx].materialIdx = 0;
        } 
    } else {
        vec3 extent = maxv - minv; 
        int axis = 0;
        if (extent.y > extent.x) axis = 1;
        if (extent.z > extent[axis]) axis = 2;
        
        float midPoint = nodes[idx].min[axis] + extent[axis] * 0.5f;

        vector<Type> leftIdxs, rightIdxs;
        for (Type t : idxs) {
            vec3 center = t.type ? spheres[t.idx].center : triangles[t.idx].c;
            if (center[axis] < midPoint) {
                leftIdxs.push_back(t);
            } else {
                rightIdxs.push_back(t);
            }
        }

        vec3 leftMax = maxv;
        vec3 leftMin = minv;
        vec3 rightMax = maxv;
        vec3 rightMin = minv;
        leftMax[axis] = midPoint;
        rightMin[axis] = midPoint;

        int leftIdx = -1;
        int rightIdx = -1;

        if (leftIdxs.empty() || rightIdxs.empty()) {
            std::sort(idxs.begin(), idxs.end(), [axis](const Type& a, const Type& b) {
                vec3 ac = a.type ? spheres[a.idx].center : vec3(triangles[a.idx].c);
                vec3 bc = b.type ? spheres[b.idx].center : vec3(triangles[b.idx].c);
                return ac[axis] < bc[axis];
            });
            size_t mid = idxs.size() / 2;
            leftIdxs.assign(idxs.begin(), idxs.begin() + mid);
            rightIdxs.assign(idxs.begin() + mid, idxs.end());
        }
        if (!leftIdxs.empty()) {
            leftIdx = nodes.size();
            buildNode(leftIdx, leftMin, leftMax,leftIdxs);
            nodes[idx].left = leftIdx;
        }
        if (!rightIdxs.empty()) {
            rightIdx = nodes.size();
            buildNode(rightIdx, rightMin, rightMax, rightIdxs);  
            nodes[idx].right = rightIdx;
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
            Sph& sph = spheres[node->idx];
            node->min = sph.center - vec3(sph.radius);
            node->max = sph.center + vec3(sph.radius);
        }
    } else {
        if (node->left != -1) tightenBounds(node->left);
        if (node->right != -1) tightenBounds(node->right);
        
        if (node->left != -1 && node->right != -1) {
            node->max = max(nodes[node->left].max, nodes[node->right].max);
            node->min = min(nodes[node->left].min, nodes[node->right].min);
        } else if (node->left != -1) {
            node->max = nodes[node->left].max;
            node->min = nodes[node->left].min;
        } else if (node->right != -1) {
            node->max = nodes[node->right].max;
            node->min = nodes[node->right].min;
        }
    }
}

void translate_object(vector<Tri>& tris, vec3 translation) {
    for (Tri& tri : tris) {
        tri.v0 += translation;
        tri.v1 += translation;
        tri.v2 += translation;
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
    }
}

void rotate_object_y(vector<Tri>& tris, float angle) {
    mat4 rotation = glm::rotate(mat4(1.0f), angle, vec3(0.0f, 1.0f, 0.0f));
    for (Tri& tri : tris) {
        tri.v0 = vec3(rotation * vec4(tri.v0, 1.0f));
        tri.v1 = vec3(rotation * vec4(tri.v1, 1.0f));
        tri.v2 = vec3(rotation * vec4(tri.v2, 1.0f));
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
    }
}

void rotate_object_x(vector<Tri>& tris, float angle) {
    mat4 rotation = glm::rotate(mat4(1.0f), angle, vec3(1.0f, 0.0f, 0.0f));
    for (Tri& tri : tris) {
        tri.v0 = vec3(rotation * vec4(tri.v0, 1.0f));
        tri.v1 = vec3(rotation * vec4(tri.v1, 1.0f));
        tri.v2 = vec3(rotation * vec4(tri.v2, 1.0f));
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
    }
}

void rotate_object_z(vector<Tri>& tris, float angle) {
    mat4 rotation = glm::rotate(mat4(1.0f), angle, vec3(0.0f, 0.0f, 1.0f));
    for (Tri& tri : tris) {
        tri.v0 = vec3(rotation * vec4(tri.v0, 1.0f));
        tri.v1 = vec3(rotation * vec4(tri.v1, 1.0f));
        tri.v2 = vec3(rotation * vec4(tri.v2, 1.0f));
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
    }
}

void scale_object(vector<Tri>& tris, float scale) {
    for (Tri& tri : tris) {
        tri.v0 *= scale;
        tri.v1 *= scale;
        tri.v2 *= scale;
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
    }
}

void add_object(vector<Tri>& tris, vector<Type>& indices, int& ind, int materialIdx = -1) {
    for (int i = 0; i < tris.size(); i++) {
        Tri tri = tris[i];
        if (materialIdx != -1) tri.materialIdx = materialIdx;
        triangles.push_back(tri);
        indices.push_back({ind, 0});
        ind++;
    }
    
}

void generate_scene(vector<Type>& indices, int& ind) {
    vector<Tri> suz = createObjectFromFile("../models/suzanne.obj");
    rotate_object_x(suz, radians(-30.0f));
    rotate_object_y(suz, radians(10.0f));
    scale_object(suz, 1.0f);
    translate_object(suz, vec3(-1.75f, 1.8f, 0.0f));

    vector<Tri> box = createObjectFromFile("../models/cornell-box.obj");
    scale_object(box, 2.0f);
    translate_object(box, vec3(0.4f, -5.0f, 8.0f));

    vector<Tri> spot = createObjectFromFile("../models/spot.obj");
    rotate_object_y(spot, radians(130.0f));
    scale_object(spot, 1.0f);
    translate_object(spot, vec3(1.2f, -1.3f, 4.2f));

    add_object(suz, indices, ind);
    add_object(box, indices, ind);
    add_object(spot, indices, ind);

    const float s = 5.0f;
    const float ts = 1.0f;
    for (int i = 0; i < N; i++) {
        Tri tri;
        vec3 j0 = vec3(rnd(-s, s), rnd(-s, s), rnd(-s, s));
        vec3 j1 = vec3(rnd(-ts, ts), rnd(-ts, ts), rnd(-ts, ts));
        vec3 j2 = vec3(rnd(-ts, ts), rnd(-ts, ts), rnd(-ts, ts));
        tri.v0 = vec3(j0);
        tri.v1 = vec3(j0 + j1);
        tri.v2 = vec3(j0 + j2);
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        tri.normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
        tri.materialIdx = 2;
        triangles.push_back(tri);
        indices.push_back({ind, 0});
        ind++;
        #if (DEBUG == 1)
        cout << "Triangle " << i << ": \n";
        cout << "  v0: (" << tri.v0.x << ", " << tri.v0.y << ", " << tri.v0.z << ")\n";
        cout << "  v1: (" << tri.v1.x << ", " << tri.v1.y << ", " << tri.v1.z << ")\n";
        cout << "  v2: (" << tri.v2.x << ", " << tri.v2.y << ", " << tri.v2.z << ")\n"; 
        #endif
    }

    for (int i = 0; i < N; i++) {
        Sph sph;
        sph.center = vec3(rnd(-s, s), rnd(-s, s), rnd(-s, s));
        sph.radius = rnd(0.1f, 0.8f);
        sph.materialIdx = 1;
        spheres.push_back(sph);
        indices.push_back({i, 1});

        #if (DEBUG == 1)
        cout << "Sphere " << i << ": \n";
        cout << "  center: (" << sph.center.x << ", " << sph.center.y << ", " << sph.center.z << ")\n";
        cout << "  radius: " << sph.radius << "\n";
        #endif
    }
}

void init(GLuint triSSBO, GLuint sphSSBO, GLuint bvhSSBO) {
    vector<Type> allIndices;
    int ind = 0;
    generate_scene(allIndices, ind);
    
    buildNode(0, vec3(-10.0f), vec3(10.0f), allIndices);
    tightenBounds(0); //? Probably not needed if bounds are calculated correctly during build
    
    vector<GPUTri> gpuTris;
    for (Tri& tri : triangles) {
        GPUTri gtri;
        gtri.v0 = vec4(tri.v0, 1.0f);
        gtri.v1 = vec4(tri.v1, 1.0f);
        gtri.v2 = vec4(tri.v2, 1.0f);
        gtri.n = vec4(tri.normal, 0.0f);
        gpuTris.push_back(gtri);
    }
    vector<GPUSph> gpuSphs;
    for (Sph& sph : spheres) {
        GPUSph gsph;
        gsph.center = sph.center;
        gsph.radius = sph.radius;
        gpuSphs.push_back(gsph);
    }

    cout << "Memory Usage:\n";
    cout << " - Triangle size: " << (gpuTris.size() * sizeof(GPUTri)) / 1000000.0 << " MB" << "\n";
    cout << " - Sphere size: " << (gpuSphs.size() * sizeof(GPUSph)) / 1000000.0 << " MB" << "\n";
    cout << " - BVH size: " << (nodes.size() * sizeof(Node)) / 1000000.0 << " MB" << "\n";
    cout << "Total Amounts:\n";
    cout << " - triangles: " << triangles.size() << "\n";
    cout << " - spheres: " << spheres.size() << "\n";
    cout << " - BVH nodes: " << nodes.size() << "\n";

    glGenBuffers(1, &triSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, gpuTris.size() * sizeof(GPUTri), gpuTris.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triSSBO); // binding = 0 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
    glGenBuffers(1, &sphSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sphSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, gpuSphs.size() * sizeof(GPUSph), gpuSphs.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sphSSBO); // binding = 1 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

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
    createLights();

    vec2 mousePos = vec2(0.0f);
    GLuint mouseUBO;
    glGenBuffers(1, &mouseUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, mouseUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(vec2), &mousePos, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, mouseUBO); // binding = 2 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    Material defaultMat;
    defaultMat.color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
    defaultMat.reflectivity = 0.0f;
    defaultMat.translucency = 0.0f;
    defaultMat.emission = 0.0f;
    defaultMat.refractiveIndex = 1.0f;
    materials.push_back(defaultMat);

    Material reflectiveRed;
    reflectiveRed.color = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    reflectiveRed.reflectivity = 0.6f;
    reflectiveRed.translucency = 0.0f;
    reflectiveRed.emission = 0.0f;
    reflectiveRed.refractiveIndex = 1.0f;
    materials.push_back(reflectiveRed);

    Material translucentBlue;
    translucentBlue.color = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    translucentBlue.reflectivity = 0.0f;
    translucentBlue.translucency = 0.2f;
    translucentBlue.emission = 0.0f;
    translucentBlue.refractiveIndex = 1.5f;
    materials.push_back(translucentBlue);

    Material emissiveGreen;
    emissiveGreen.color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
    emissiveGreen.reflectivity = 0.0f;
    emissiveGreen.translucency = 0.0f;
    emissiveGreen.emission = 0.5f;
    emissiveGreen.refractiveIndex = 1.0f;
    materials.push_back(emissiveGreen);

    materialMap["Khaki"] = 0;
    materialMap["BloodyRed"] = 1;
    materialMap["DarkGreen"] = 2;
    materialMap["Light"] = 3;

    GLuint triSSBO, sphSSBO, bvhSSBO, materialSSBO;
    glGenBuffers(1, &materialSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, materials.size() * sizeof(Material), materials.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, materialSSBO); // binding = 3 for SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0); 

    float initialTime = glfwGetTime();
    init(triSSBO, sphSSBO, bvhSSBO);
    cout << "BVH build time: " << (glfwGetTime() - initialTime) << " seconds\n";

    #if (DEBUG == 1)
    for (Node& n : nodes) {
        string type = (n.type == 0) ? "⚠️" : (n.type == 1) ? "⭕" : "🌲";
        cout << "Node " << n.ownIdx << ": Index=" << n.idx << ", type=" << type << ", left=" << n.left << ", right=" << n.right << "\n";
        cout << "  Min: (" << n.min.x << ", " << n.min.y << ", " << n.min.z << ")\n";
        cout << "  Max: (" << n.max.x << ", " << n.max.y << ", " << n.max.z << ")\n";
        cout << "  Extent: (" << (n.max.x - n.min.x) << ", " << (n.max.y - n.min.y) << ", " << (n.max.z - n.min.z) << ")\n";
    }
    #endif

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
        if (xpos != mousePos.x || ypos != HEIGHT - mousePos.y) {
            mousePos = vec2(xpos, HEIGHT - ypos);
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

            stringstream title;
            title << "Raytracer - " << fps << " FPS (" << frameTimeMs << " ms/frame)";
            glfwSetWindowTitle(window, title.str().c_str());

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