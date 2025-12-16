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

struct GPUTri {
    vec4 data0; // v0.x, v0.y, v0.z, e1.x
    vec4 data1; // e1.y, e1.z, e2.x, e2.y
    vec4 data2; // e2.z, normal.x, normal.y, normal.z
};

struct GPUSph {
    vec4 data0; // center.x, center.y, center.z, radius
};

struct Material { //TODO: compact this
    vec4 color;
    float reflectivity;
    float translucency;
    float emission;
    float refractiveIndex;
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

struct GPUNode {
    vec4 data0; // min.x, min.y, min.z, leftOrStart
    vec4 data1; // max.x, max.y, max.z, count
};

struct Type {
    int idx;
    int type; // 0 = triangle, 1 = sphere
};

struct Mesh {
    int materialIdx;
    int bvhRoot;
    int triStart;
    int triCount;
};

static_assert(sizeof(GPUTri) == 48, "GPUTri size incorrect");
static_assert(sizeof(Node) == 48, "Node size incorrect");
static_assert(sizeof(GPUSph) == 16, "GPUSph size incorrect");
static_assert(sizeof(GPUNode) == 32, "GPUNode size incorrect");


extern std::vector<Node> nodes;
extern std::vector<Sph> spheres;
extern std::vector<Mesh> meshes;
extern std::vector<Tri> triangles;
extern std::vector<int> triIndices;
extern std::vector<Material> materials;
extern std::unordered_map<std::string, int> materialMap;