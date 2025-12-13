#version 430 core

const int DEPTH = 4;
const float EPSILON = 1e-6;
const float MINSILON = 1e-3;
const float MAXILON = 1e6;
const int MAX_STACK_SIZE = 64;
const float MAX_RAY_DISTANCE = 200.0;
const float MAX_RAY_DENSITY = 100.0;

struct Triangle { //TODO: compact this better
    vec3 v0; float _pad0;
    vec3 v1; float _pad1;
    vec3 v2; float _pad2;
    vec3 n;  float _pad3;
};

struct Sphere {
    vec3 center;
    float radius;
};

struct Node { //TODO: compact this better
    vec3 min;
    int left;
    vec3 max;
    int right;
    int start;
    int count;
    int mat;
    float _pad0;
};

struct Material {
    vec3 color; float _pad0;
    float reflectivity;
    float translucency;
    float emission;
    float refractiveIndex;
};

struct Hit {
    float t;
    Node node;
    Material mat;
    vec3 Q;
    vec3 N;
};

layout (local_size_x = 8, local_size_y = 8) in;

layout (rgba32f, binding = 0) uniform image2D imgOutput;

layout (std140, binding = 0) uniform CameraData {
    vec3 camPos;
    float fov;
    vec3 camForward;
    float aspect;
    vec3 camUp;
    int moved;
};

layout (std140, binding = 1) uniform LightData { vec3 lightPos; float lightIntensity; };

layout (std140, binding = 2) uniform Mouse { vec2 mousePos; };

layout (std140, binding = 3) uniform Time { int time; };

layout (std430, binding = 0) buffer Triangles { Triangle triangles[]; };

layout (std430, binding = 1) buffer Spheres { Sphere spheres[]; };

layout (std430, binding = 2) buffer BVH { Node nodes[]; };

layout (std430, binding = 3) buffer Materials { Material materials[]; };

layout (std430, binding = 4) buffer TriIndices { int triIndices[]; };

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

float findSphereIntersection(vec3 rayOri, vec3 rayDir, int i) {
    Sphere sph = spheres[i];
    vec3 oc = rayOri - sph.center;
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - sph.radius * sph.radius;
    float d = b * b - 4.0 * a * c;
    if (d < 0.0) return MAXILON;

    float sqrtD = sqrt(d);
    float t0 = (-b - sqrtD) / (2.0 * a);
    if (t0 > EPSILON) return t0;

    float t1 = (-b + sqrtD) / (2.0 * a);
    if (t1 > EPSILON) return t1;

    return MAXILON;
}

float intersectAABB(vec3 rayOri, vec3 rayDir, vec3 minBound, vec3 maxBound) {
    vec3 tlow = (minBound - rayOri) / rayDir; //TODO: Precompute inverse of ray direction
    vec3 thigh = (maxBound - rayOri) / rayDir;
    vec3 tmin = min(tlow, thigh);
    vec3 tmax = max(tlow, thigh);
    float tclose = max(max(tmin.x, tmin.y), tmin.z);
    float tfar = min(min(tmax.x, tmax.y), tmax.z);
    bool hit = tfar >= tclose && tfar >= 0.0;
    return hit ? tclose : MAXILON;
}

Hit traverseBVH(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    float closestB = MAXILON;
    int closestN = -1;
    int closestTri = -1;

    int stack[MAX_STACK_SIZE];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    while (stackPtr > 0) {
        int child = stack[--stackPtr];
        Node node = nodes[child];

        float childT = intersectAABB(rayOri, rayDir, node.min, node.max);
        if (childT >= closestT) continue;

        if (node.left == -1 && node.right == -1) {
            for (int i = node.start; i < node.start + node.count; i++) {
                float t = findTriangleIntersection(rayOri, rayDir, triIndices[i]);
                if (t > 0.0 && t < closestT) {
                    closestT = t;
                    closestTri = triIndices[i];
                    closestN = child;
                }
            }
        } else {
            if (node.left >= 0 && node.right >= 0) {
                float leftT = intersectAABB(rayOri, rayDir, nodes[node.left].min, nodes[node.left].max);
                float rightT = intersectAABB(rayOri, rayDir, nodes[node.right].min, nodes[node.right].max);
                if (leftT < rightT) {
                    if (node.right >= 0) stack[stackPtr++] = node.right;
                    if (node.left >= 0) stack[stackPtr++] = node.left;
                } else {
                    if (node.left >= 0) stack[stackPtr++] = node.left;
                    if (node.right >= 0) stack[stackPtr++] = node.right;
                }
            } 
            else if (node.left >= 0) {
                stack[stackPtr++] = node.left;
            } 
            else if (node.right >= 0) {
                stack[stackPtr++] = node.right;
            }
        }
    }

    if (closestN == -1 || closestT == MAXILON) {
        Hit noHit;
        noHit.t = MAXILON;
        return noHit;
    }

    Hit hit;
    hit.t = closestT;
    hit.node = nodes[closestN];
    hit.mat = materials[hit.node.mat];
    hit.Q = rayOri + hit.t * rayDir;
    hit.N = triangles[closestTri].n;
    return hit;
}

Hit intersectSpheres(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    int closestIdx = -1;
    for (int i = 0; i < spheres.length(); i++) {
        float t = findSphereIntersection(rayOri, rayDir, i);
        if (t < closestT) {
            closestT = t;
            closestIdx = i;
        }
    }
    if (closestIdx == -1 || closestT == MAXILON) {
        Hit noHit;
        noHit.t = MAXILON;
        return noHit;
    }
    Hit hit;
    hit.t = closestT;
    hit.mat = materials[1];
    hit.Q = rayOri + hit.t * rayDir;
    hit.N = normalize(hit.Q - spheres[closestIdx].center);
    return hit;
}

Hit getHit(vec3 rayOri, vec3 rayDir) {
    Hit sphHit = intersectSpheres(rayOri, rayDir);
    Hit triHit = traverseBVH(rayOri, rayDir);
    return (sphHit.t < triHit.t) ? sphHit : triHit;
}

vec4 getColorRay(vec3 rayOri, vec3 rayDir) {
    vec3 color = vec3(0.0);
    struct Ray {
        vec3 ori;
        vec3 dir;
    };

    int depth = 0;
    Ray stack[MAX_STACK_SIZE];
    int stackPtr = 0;
    stack[stackPtr++] = Ray(rayOri, rayDir);
    while (stackPtr > 0) {
        Ray ray = stack[--stackPtr];
        if (depth > DEPTH) continue;

        Hit hit = getHit(ray.ori, ray.dir);
        if (hit.t == MAXILON) continue;

        vec3 N = faceforward(hit.N, ray.dir, hit.N);

        if (hit.mat.translucency > 0.0) {
            bool entering = dot(N, ray.dir) < 0.0;
            vec3 Ntrans = entering ? N : -N;
            float eta = entering ? (1.0 / hit.mat.refractiveIndex) : hit.mat.refractiveIndex;
            vec3 rd = refract(normalize(ray.dir), Ntrans, eta);
            if (length(rd) > 0.0) {
                vec3 offset = entering ? (-Ntrans * MINSILON) : (Ntrans * MINSILON);
                stack[stackPtr++] = Ray(hit.Q + offset, normalize(rd));
                depth++;
            }
        } else {
            vec3 lightDir = normalize(lightPos - hit.Q);
            Hit shadowHit = traverseBVH(hit.Q + N * MINSILON, lightDir);
            float lightDist = length(lightPos - hit.Q);
            if (shadowHit.t < lightDist) continue;
        }

        if (hit.mat.reflectivity > 0.0) {
            stack[stackPtr++] = Ray(hit.Q + N * MINSILON, normalize(reflect(ray.dir, N)));
            depth++;
        }

        if (hit.mat.emission > 0.0) {
            color += hit.mat.color * hit.mat.emission;
        }

        vec3 diffuse = abs(dot(N, normalize(lightPos - hit.Q))) * hit.mat.color;

        color += diffuse * (1.0 - hit.mat.reflectivity - hit.mat.translucency);
    }
    return vec4(color, 1.0);
}

vec3 addJitter(vec3 dir, inout uint rng) {
    float u1 = float(rng) / 4294967295.0;
    rng = rng * 1664525u + 1013904223u;
    float u2 = float(rng) / 4294967295.0;
    rng = rng * 1664525u + 1013904223u;

    float r = sqrt(u1);
    float theta = 2.0 * 3.14159265359 * u2;

    vec3 w = normalize(dir);
    vec3 u = normalize(cross(abs(w.x) > 0.1 ? vec3(0,1,0) : vec3(1,0,0), w));
    vec3 v = cross(w, u);

    vec3 jitteredDir = r * cos(theta) * u + r * sin(theta) * v + sqrt(1.0 - u1) * w;
    return normalize(jitteredDir);
}

vec4 getColorPath(vec3 rayOri, vec3 rayDir, vec3 prevColor, int frames) {
    vec3 collectedLight = vec3(0.0);

    uvec3 gid = gl_GlobalInvocationID;
    uint rng = uint(gid.x) * 1973u ^ uint(gid.y) * 9277u ^ uint(gid.z) * 2663u ^ 0x9E3779B9u;
    rng += uint(frames) * 1013904223u;

    for (int bounce = 0; bounce < DEPTH; bounce++) {
        Hit hit = traverseBVH(rayOri, rayDir);
        if (hit.t == MAXILON) break;

        vec3 N = faceforward(hit.N, rayDir, hit.N);

        if (hit.mat.emission > 0.0) {
            collectedLight += hit.mat.color * hit.mat.emission;
            break;
        }

        if (hit.mat.reflectivity > 0.0) {
            rayDir = addJitter(reflect(rayDir, N), rng);
            rayOri = hit.Q + N * MINSILON;
            continue;
        }

        if (hit.mat.translucency > 0.0) {
            bool entering = dot(N, rayDir) < 0.0;
            vec3 Ntrans = entering ? N : -N;
            float eta = entering ? (1.0 / hit.mat.refractiveIndex) : hit.mat.refractiveIndex;
            vec3 rd = refract(normalize(rayDir), Ntrans, eta);
            if (length(rd) > 0.0) {
                rayDir = addJitter(normalize(rd), rng);
                vec3 offset = entering ? (-Ntrans * MINSILON) : (Ntrans * MINSILON);
                rayOri = hit.Q + offset;
                continue;
            } else {
                rayDir = addJitter(reflect(rayDir, N), rng);
                rayOri = hit.Q + N * MINSILON;
                continue;
            }
        }
    }

    if (frames == 0) {
        return vec4(collectedLight, 1.0);
    } else {
        vec3 accumColor = prevColor * float(frames) + collectedLight;
        accumColor /= float(frames + 1);
        return vec4(accumColor, 1.0);
    }
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(imageSize(imgOutput));
    uv = uv * 2.0 - 1.0;
    uv.x *= aspect;
    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * normalize(cross(camForward, camUp)) + uv.y * tan(fov / 2.0) * camUp);
    int d = int(floor(distance(vec2(pixel), mousePos))); //TODO: Use texture to get distance and sampling info
    vec4 color = getColorRay(camPos, rayDir);
    if (d < MAX_RAY_DISTANCE) {
        int ray_density = int((MAX_RAY_DISTANCE - d) / MAX_RAY_DENSITY); //TODO: Handle this better
        ray_density = min(ray_density, 5);
        for (int y = -ray_density; y <= ray_density; y++) {
            for (int x = -ray_density; x <= ray_density; x++) {
                if (x == 0 && y == 0) continue;
                vec2 offset = vec2(float(x), float(y)) * 0.001;
                vec2 offsetUV = uv + offset;
                vec3 offsetRayDir = normalize(camForward + offsetUV.x * tan(fov / 2.0) * normalize(cross(camForward, camUp)) + offsetUV.y * tan(fov / 2.0) * camUp);
                color += getColorRay(camPos, offsetRayDir);
            }
        }
        float totalRays = float((2 * ray_density + 1) * (2 * ray_density + 1));
        color /= totalRays;
    }
    imageStore(imgOutput, pixel, color);

    //vec4 prevColor = imageLoad(imgOutput, pixel);
    //vec4 color = getColorPath(camPos, rayDir, prevColor.rgb, time);
    //imageStore(imgOutput, pixel, color);
}
