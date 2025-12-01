#version 430 core

const float EPSILON = 1e-6;
const float MAXILON = 1e6;

struct Triangle { // 64 bytes
    vec3 v0; float _pad0;
    vec3 v1; float _pad1;
    vec3 v2; float _pad2;
    vec3 c;  float _pad3;
};

struct Node { // 48 bytes
    vec3 min;
    int triIdx;
    vec3 max;
    int ownIdx;
    int leftIdx;
    int rightIdx;
    float _pad0;
    float _pad1;
};

layout (local_size_x = 8, local_size_y = 8) in;

layout (rgba32f, binding = 0) uniform image2D imgOutput;

layout (std140, binding = 0) uniform CameraData {
    vec3 camPos;
    float fov;
    vec3 camForward;
    float aspect;
    vec3 camUp;
    float _pad;
};

layout (std140, binding = 1) uniform LightData { vec3 lightPos; float lightIntensity; };

layout (std140, binding = 2) uniform Mouse { vec2 mousePos; };

layout (std430, binding = 0) buffer Triangles { Triangle triangles[]; };

layout (std430, binding = 1) buffer BVH { Node nodes[]; };

float findTriangleIntersection(vec3 rayOrigin, vec3 rayDir, int i) {
    Triangle tri = triangles[i];
    vec3 edge1 = tri.v1 - tri.v0;
    vec3 edge2 = tri.v2 - tri.v0;
    vec3 h = cross(rayDir, edge2);
    float a = dot(edge1, h);
    if (abs(a) < EPSILON) return MAXILON;
    float f = 1.0 / a;
    vec3 s = rayOrigin - tri.v0;
    float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return MAXILON;
    vec3 q = cross(s, edge1);
    float v = f * dot(rayDir, q);
    if (v < 0.0 || u + v > 1.0) return MAXILON;
    float t = f * dot(edge2, q);
    return (t > EPSILON) ? t : MAXILON;
}

bool intersectAABB(vec3 rayOri, vec3 rayDir, vec3 minBound, vec3 maxBound) {
    vec3 tlow = (minBound - rayOri) / rayDir;
    vec3 thigh = (maxBound - rayOri) / rayDir;
    vec3 tmin = min(tlow, thigh);
    vec3 tmax = max(tlow, thigh);
    float tclose = max(max(tmin.x, tmin.y), tmin.z);
    float tfar = min(min(tmax.x, tmax.y), tmax.z);
    return tfar >= tclose && tfar >= 0.0;
}

float traverseBVH(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    int stack[128];
    int stackPtr = 0;
    stack[stackPtr++] = 0;
    while (stackPtr > 0) {
        int nodeIdx = stack[--stackPtr];
        Node node = nodes[nodeIdx];

        if (!intersectAABB(rayOri, rayDir, node.min, node.max)) continue;
        if (node.triIdx >= 0) {
            float t = findTriangleIntersection(rayOri, rayDir, node.triIdx);
            if (t > 0.0 && t < closestT) closestT = t;
        } else {
            if (node.rightIdx >= 0) stack[stackPtr++] = node.rightIdx;
            if (node.leftIdx >= 0)  stack[stackPtr++] = node.leftIdx;
        }
    }

    return closestT;
}

float intersectAllTriangles(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    for (int i = 0; i < triangles.length(); i++) {
        float t = findTriangleIntersection(rayOri, rayDir, i);
        if (t > 0.0 && t < closestT) {
            closestT = t;
        }
    }
    return closestT;
}

vec4 getColor(vec3 rayOri, vec3 rayDir) {
    float t = traverseBVH(rayOri, rayDir);
    if (t == MAXILON) return vec4(0.5, 0.7, 1.0, 1.0);
    return vec4(1.0);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(imageSize(imgOutput));
    uv = uv * 2.0 - 1.0;
    uv.x *= aspect;
    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * normalize(cross(camForward, camUp)) + uv.y * tan(fov / 2.0) * camUp);

    vec4 color = getColor(camPos, rayDir);

    imageStore(imgOutput, pixel, color);
}
