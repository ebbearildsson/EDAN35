#version 430 core

const float EPSILON = 1e-3;
const float MAXILON = 1e6;

struct Triangle {
    vec3 v0;
    float nx;
    vec3 v1;
    float ny;
    vec3 v2;
    float nz;
};

struct Sphere {
    vec3 center;
    float radius;
};

struct GeoNode {
    vec3 vertex;
    float radius;
};

struct Node {
    vec3 min;
    float _pad0;
    vec3 max;
    float _pad1;
    int left;
    int right;
    int start;
    int count;
};

struct Mesh {
    vec3 lowerLeftBack;
    int offset;
    vec3 upperRightFront;
    int size;
    vec3 color;
    int type; // 0 = triangle mesh, 1 = sphere mesh
    float reflectivity;
    float transperency;
    float emmission;
    float _pad;
};

struct Hit {
    vec3 n;
    float t;
    vec3 _pad;
    int m;
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

layout (std140, binding = 2) uniform Mouse { vec2 mousePos; };

layout (std430, binding = 1) buffer Triangles { Triangle triangles[]; };

layout (std430, binding = 2) buffer Meshes { Mesh meshes[]; };

layout (std430, binding = 3) buffer Spheres { Sphere spheres[]; };

layout (std430, binding = 4) buffer Geometry { GeoNode geometry[]; };

layout (std430, binding = 5) buffer BVH { Node nodes[]; };

float findTriangleIntersection(vec3 rayOrigin, vec3 rayDir, Triangle tri) {
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

float findSphereIntersection(vec3 rayOrigin, vec3 rayDir, Sphere sph) {
    vec3 oc = rayOrigin - sph.center;
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - sph.radius * sph.radius;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return MAXILON;
    return (-b - sqrt(discriminant)) / (2.0 * a);
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
    // BVH traversal logic would go here
    return MAXILON;
}

Hit findClosestIntersection(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    int closestI;
    int closestM;
    for (int m = 0; m < meshes.length(); m++) {
        Mesh mesh = meshes[m];
        bool intersect = intersectAABB(rayOri, rayDir, mesh.lowerLeftBack, mesh.upperRightFront);
        if (!intersect) continue;

        float t;
        switch (mesh.type) {
            case 0: // Triangle mesh
                for (int i = mesh.offset; i <= mesh.offset + mesh.size; i++) {
                    t = findTriangleIntersection(rayOri, rayDir, triangles[i]);
                    if (t > 0.0 && t < closestT) {
                        closestT = t;
                        closestI = i;
                        closestM = m;
                    }
                }
                break;
            case 1: // Sphere mesh
                for (int i = mesh.offset; i <= mesh.offset + mesh.size; i++) {
                    t = findSphereIntersection(rayOri, rayDir, spheres[i]);
                    if (t > 0.0 && t < closestT) {
                        closestT = t;
                        closestI = i;
                        closestM = m;
                    }
                }
                break;
            default:
                continue;
        }
    }

    Hit hit;
    hit.t = closestT;
    hit.m = closestM;
    switch (meshes[closestM].type) {
        case 0:
            Triangle tri = triangles[closestI];
            hit.n = vec3(tri.nx, tri.ny, tri.nz);
            break;
        case 1:
            hit.n = normalize((rayOri + rayDir * closestT) - spheres[closestI].center);  
            break;
        default:
            break;
    }

    return hit;
}

vec4 getColor(vec3 rayOri, vec3 rayDir) {
    Hit hit = findClosestIntersection(rayOri, rayDir);
    
    if (hit.t == MAXILON) return vec4(0.0);

    vec3 Q = rayOri + rayDir * hit.t;
    vec3 lightDir = normalize(lightPos - Q);
    float diff = max(dot(hit.n, lightDir), 0.0);
    
    vec3 biasQ = Q + hit.n * 1e-3;

    Hit shadowHit = findClosestIntersection(biasQ, lightDir);
    if (shadowHit.t <= length(lightPos - Q)) return vec4(0.0);
    
    Mesh mesh = meshes[hit.m];
    if (mesh.reflectivity > 0.0) {
        const int MAX_REFLECTIONS = 3;
        Mesh currMesh = mesh;
        vec3 currOri = biasQ;
        vec3 accumColor = vec3(0.0);
        vec3 currDir = reflect(rayDir, hit.n);
        for (int bounce = 0; bounce < MAX_REFLECTIONS; bounce++) {
            Hit reflectHit = findClosestIntersection(currOri, currDir);
            if (reflectHit.t == MAXILON) break;
            currMesh = meshes[reflectHit.m];
            vec3 tempColor = accumColor;
            float currDiff = max(dot(reflectHit.n, lightDir), 0.0);
            currOri = currOri + currDir * reflectHit.t + reflectHit.n * 1e-3;
            Hit shadowHit = findClosestIntersection(currOri, normalize(lightPos - currOri));
            if (shadowHit.t <= length(lightPos - currOri)) currDiff = 0.0;
            accumColor += (currMesh.color * pow(currMesh.reflectivity, bounce + 2)) * currDiff;
            if (currMesh.reflectivity <= 0.0 || accumColor == tempColor) break;
            currDir = reflect(currDir, reflectHit.n);
        }

        vec3 finalColor = mix(mesh.color, accumColor, mesh.reflectivity);
        return vec4(finalColor * diff * lightIntensity, 1.0);
    }
    
    //return vec4(mesh.color, 1.0);
    return vec4(mesh.color * diff * lightIntensity, 1.0);
    //return vec4(abs(hit.n), 1.0);
    //return vec4(((mesh.type + 1.0) / 2.0) * vec3(1.0), 1.0);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(imgOutput);
    vec2 uv = (vec2(pixel) / vec2(size)) * 2.0 - 1.0;
    uv.x *= aspect;

    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * vec3(1.0, 0.0, 0.0) + uv.y * tan(fov / 2.0) * camUp);

    vec4 color = getColor(camPos, rayDir);

    imageStore(imgOutput, ivec2(gl_GlobalInvocationID.xy), color);
}
