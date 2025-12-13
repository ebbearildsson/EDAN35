#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <unordered_map>

using namespace glm;

struct Config {
    const static int width = 600;
    const static int height = 600;
    const static int Num = 10;
    const static int maxBVHDepth = 32;
    const static int minVolumeAmount = 2;
};

struct Tri {
    vec3 v0;
    vec3 v1;
    vec3 v2;
    vec3 c;
    vec3 normal;
    vec3 min;
    vec3 max;
    int materialIdx;
};

struct Sph {
    vec3 center;
    float radius;
    int materialIdx;
};

struct Camera {
    vec3 position;
    float fov;
    vec3 forward;
    float aspect;
    vec3 up;
    int moved;
};

struct Light {
    vec3 direction;
    float intensity;
};

struct GPUTri { //TODO: compact this
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 n;
};

struct GPUSph {
    vec3 center;
    float radius;
};

struct Material { //TODO: compact this
    vec4 color;
    float reflectivity;
    float translucency;
    float emission;
    float refractiveIndex;
};

struct OldNode { //TODO: compact this
    vec3 min;
    int idx;
    vec3 max;
    int ownIdx; //TODO: remove
    int left;
    int right;
    int type;
    int materialIdx;
};

struct Node {
    vec3 min;
    int left;
    vec3 max;
    int right;
    int start;
    int count;
    int mat;
    float _pad0;
};

struct Type {
    int idx;
    int type; // 0 = triangle, 1 = sphere
};

static_assert(sizeof(GPUTri) == 64, "GPUTri size incorrect");
static_assert(sizeof(Node) == 48, "Node size incorrect");
static_assert(sizeof(GPUSph) == 16, "GPUSph size incorrect");

extern std::vector<Node> nodes;
extern std::vector<Tri> triangles;
extern std::vector<int> triIndices;
extern std::vector<Sph> spheres;
extern std::vector<Material> materials;
extern std::unordered_map<std::string, int> materialMap;