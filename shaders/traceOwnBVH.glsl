#version 430 core

const float EPSILON = 1e-3;
const float MAXILON = 1e6;

struct Triangle { vec3 v0; vec3 v1; vec3 v2; vec3 c; };

struct Node {
    vec3 aabbMin;
    int triangleIndex;
    vec3 aabbMax;
    float _pad1;
    int leftIdx;
    int rightIdx;
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

float findTriangleIntersection(vec3 rayOrigin, vec3 rayDir, Triangle tri) {
    const vec3 edge1 = tri.v1 - tri.v0;
    const vec3 edge2 = tri.v2 - tri.v0;
    const vec3 h = cross(rayDir, edge2);
    const float a = dot(edge1, h);
    if (a > -EPSILON && a < EPSILON) return MAXILON;
    const float f = 1 / a;
    const vec3 s = rayOrigin - tri.v0;
    const float u = f * dot(s, h);
    if (u < 0 || u > 1) return MAXILON;
    const vec3 q = cross(s, edge1);
    const float v = f * dot(rayDir, q);
    if (v < 0 || u + v > 1) return MAXILON;
    const float t = f * dot(edge2, q);
    if (t > EPSILON) return t;
    else return MAXILON;
}

bool IntersectAABB(vec3 rayOri, vec3 rayDir, vec3 minBound, vec3 maxBound) {
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
    int stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    while (stackPtr > 0) {
        int nodeIdx = stack[--stackPtr];
        Node node = nodes[nodeIdx];

        if (!IntersectAABB(rayOri, rayDir, node.aabbMin, node.aabbMax)) continue;

        if (node.triangleIndex >= 0) {
            Triangle tri = triangles[node.triangleIndex];
            float t = findTriangleIntersection(rayOri, rayDir, tri);
            if (t < closestT) closestT = t;
        } else {
            if (node.rightIdx >= 0) stack[stackPtr++] = node.rightIdx;
            if (node.leftIdx >= 0)  stack[stackPtr++] = node.leftIdx;
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
