#version 430 core

const float EPSILON = 1e-3;
const float MAXILON = 1e6;

struct Triangle { vec3 v0; vec3 v1; vec3 v2; vec3 centroidd; };

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

struct BVHNode {
    vec3 aabbMin;
    vec3 aabbMax;
    uint leftFirst;
    uint triCount;
};

layout (std430, binding = 1) buffer BVH { BVHNode nodes[]; };

layout (std430, binding = 2) buffer TriIndices { uint triIdx[]; };

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

float IntersectBVH(vec3 rayO, vec3 rayD, uint rootIdx ) {
    float t = MAXILON;
    const int MAX_STACK = 128;
    uint stack[MAX_STACK];
    int sp = 0;
    stack[sp++] = rootIdx;

    while (sp > 0) {
        uint nodeIdx = stack[--sp];
        BVHNode node = nodes[nodeIdx];

        if (!IntersectAABB(rayO, rayD, node.aabbMin, node.aabbMax)) continue;

        if (node.triCount > 0u) {
            for (uint i = 0u; i < node.triCount; ++i) {
                uint triIndex = triIdx[node.leftFirst + i];
                float inter = findTriangleIntersection(rayO, rayD, triangles[triIndex]);
                if (inter > 0.0 && inter < t) t = inter;
            }
        } else {
            if (sp + 2 <= MAX_STACK) {
                stack[sp++] = node.leftFirst + 1u;
                stack[sp++] = node.leftFirst;
            } else if (sp + 1 <= MAX_STACK) {
                stack[sp++] = node.leftFirst;
            }
        }
    }
    return t;
}

vec4 getColor(vec3 rayOri, vec3 rayDir) {
    float t = IntersectBVH(rayOri, rayDir, 0u);
    if (t == MAXILON) return vec4(0.0);
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
