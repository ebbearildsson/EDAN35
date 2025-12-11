
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <structs.hh>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

using namespace glm;
using namespace std;

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

vector<Tri> createObjectFromFile(const string& path, std::unordered_map<string, int>& materialMap) {
    vector<vec3> temp_vertices;
    vector<vec3> temp_normals;
    vector<Tri> tris;
    ifstream file(path);
    // if (!file.is_open()) throw runtime_error("Failed to open OBJ file: " + path);
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
    // if (!file.is_open()) throw runtime_error("Failed to open file: " + path);
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

void createCamera(GLuint &cameraUBO, Camera &cam, int width, int height) {
    cam = {
        vec3(0.0f, 0.0f, 20.0f),
        radians(45.0f),
        vec3(0.0f, 0.0f, -1.0f),
        (float)width / (float)height,
        vec3(0.0f, 1.0f, 0.0f),
        0.0f
    };
    glGenBuffers(1, &cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Camera), &cam, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, cameraUBO); // binding = 0 for UBO
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
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
        tri.normal = normalize(vec3(rotation * vec4(tri.normal, 0.0f)));
    }
}

void rotate_object_x(vector<Tri>& tris, float angle) {
    mat4 rotation = glm::rotate(mat4(1.0f), angle, vec3(1.0f, 0.0f, 0.0f));
    for (Tri& tri : tris) {
        tri.v0 = vec3(rotation * vec4(tri.v0, 1.0f));
        tri.v1 = vec3(rotation * vec4(tri.v1, 1.0f));
        tri.v2 = vec3(rotation * vec4(tri.v2, 1.0f));
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        tri.normal = normalize(vec3(rotation * vec4(tri.normal, 0.0f)));
    }
}

void rotate_object_z(vector<Tri>& tris, float angle) {
    mat4 rotation = glm::rotate(mat4(1.0f), angle, vec3(0.0f, 0.0f, 1.0f));
    for (Tri& tri : tris) {
        tri.v0 = vec3(rotation * vec4(tri.v0, 1.0f));
        tri.v1 = vec3(rotation * vec4(tri.v1, 1.0f));
        tri.v2 = vec3(rotation * vec4(tri.v2, 1.0f));
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        tri.normal = normalize(vec3(rotation * vec4(tri.normal, 0.0f)));
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

void add_object(vector<Tri>& tris, vector<Type>& indices, vector<Tri>& triangles, int& ind, int materialIdx = -1) {
    for (int i = 0; i < tris.size(); i++) {
        Tri tri = tris[i];
        if (materialIdx != -1) tri.materialIdx = materialIdx;
        triangles.push_back(tri);
        indices.push_back({ind, 0});
        ind++;
    }
}