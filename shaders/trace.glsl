#version 430 core

struct Triangle {
    vec3 v0;
    float emission;
    vec3 v1;
    float reflectivity;
    vec3 v2;
    float translucancy;
    vec4 color;
};

struct Mesh {
    vec3 lowerLeftBack;
    int triangleOffset;
    vec3 upperRightFront;
    int triangleCount;
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

layout (std140, binding = 1) uniform LightData {
    vec3 lightPos;
    float lightIntensity;
};

layout (std430, binding = 1) buffer Triangles { Triangle triangles[]; };

layout (std430, binding = 2) buffer Meshes { Mesh meshes[]; };

float findIntersection(vec3 rayOrigin, vec3 rayDir, Triangle tri) {
    vec3 edge1 = tri.v1 - tri.v0;
    vec3 edge2 = tri.v2 - tri.v0;
    vec3 h = cross(rayDir, edge2);
    float a = dot(edge1, h);

    if (abs(a) < 0.0001) return -1.0;

    float f = 1.0 / a;
    vec3 s = rayOrigin - tri.v0;
    float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return -1.0;

    vec3 q = cross(s, edge1);
    float v = f * dot(rayDir, q);
    if (v < 0.0 || u + v > 1.0) return -1.0;

    return f * dot(edge2, q);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(imgOutput);
    vec2 uv = (vec2(pixel) / vec2(size)) * 2.0 - 1.0;
    uv.x *= aspect;

    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * vec3(1.0, 0.0, 0.0) + uv.y * tan(fov / 2.0) * vec3(0.0, 1.0, 0.0));

    float closestT = 1e30;
    Triangle closestTri;
    Mesh closestMesh;
    for (int m = 0; m < meshes.length(); m++) {
        Mesh mesh = meshes[m];
        vec3 tlow = (mesh.lowerLeftBack - camPos) / rayDir;
        vec3 thigh = (mesh.upperRightFront - camPos) / rayDir;
        vec3 tmin = min(tlow, thigh);
        vec3 tmax = max(tlow, thigh);
        float tclose = max(max(tmin.x, tmin.y), tmin.z);
        float tfar = min(min(tmax.x, tmax.y), tmax.z);

        if (tclose > tfar) continue;
        for (int i = mesh.triangleOffset; i <= mesh.triangleOffset + mesh.triangleCount; i++) {
            Triangle tri = triangles[i];
            float t = findIntersection(camPos, rayDir, tri);
            if (t > 0.00001 && t < closestT) {
                closestT = t;
                closestTri = tri;
                closestMesh = mesh;
            }
        }
    }
    if (closestT == 1e30) {
        imageStore(imgOutput, ivec2(gl_GlobalInvocationID.xy), vec4(0.0));
        return;
    }
    vec3 edge1 = closestTri.v1 - closestTri.v0;
    vec3 edge2 = closestTri.v2 - closestTri.v0;
    vec3 Q = camPos + rayDir * closestT;
    vec3 lightDir = normalize(lightPos - Q);
    float diff = max(dot(normalize(cross(edge1, edge2)), lightDir), 0.0);
    vec4 hitColor = closestTri.color * diff * lightIntensity + vec4(closestTri.emission);

    imageStore(imgOutput, ivec2(gl_GlobalInvocationID.xy), hitColor);
}
