#version 430 core

const float EPSILON = 1e-6;
const float MINSILON = 1e-3;
const float MAXILON = 1e6;
const int MAX_STACK_SIZE = 128;
const int MAX_REFLECTION_DEPTH = 5;
const int MAX_REFRACTION_DEPTH = 5;
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
    int idx;
    vec3 max;
    int ownIdx;
    int leftIdx;
    int rightIdx;
    int type;
    int matIdx;
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
    float _pad;
};

layout (std140, binding = 1) uniform LightData { vec3 lightPos; float lightIntensity; };

layout (std140, binding = 2) uniform Mouse { vec2 mousePos; };

layout (std430, binding = 0) buffer Triangles { Triangle triangles[]; };

layout (std430, binding = 1) buffer Spheres { Sphere spheres[]; };

layout (std430, binding = 2) buffer BVH { Node nodes[]; };

layout (std430, binding = 3) buffer Materials { Material materials[]; };

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

bool intersectAABB(vec3 rayOri, vec3 rayDir, vec3 minBound, vec3 maxBound) {
    vec3 tlow = (minBound - rayOri) / rayDir;
    vec3 thigh = (maxBound - rayOri) / rayDir;
    vec3 tmin = min(tlow, thigh);
    vec3 tmax = max(tlow, thigh);
    float tclose = max(max(tmin.x, tmin.y), tmin.z);
    float tfar = min(min(tmax.x, tmax.y), tmax.z);
    return tfar >= tclose && tfar >= 0.0;
}

//TODO: Add more primitive types
float intersect(vec3 rayOri, vec3 rayDir, Node n) {
    if (n.type == 0) {
        return findTriangleIntersection(rayOri, rayDir, n.idx);
    } else if (n.type == 1) {
        return findSphereIntersection(rayOri, rayDir, n.idx);
    }
    return MAXILON;
}

//TODO: Add more primitive types
vec3 getNormal(vec3 point, Node node) {
    if (node.type == 0) {
        Triangle tri = triangles[node.idx];
        return tri.n;
    } else if (node.type == 1) {
        Sphere sph = spheres[node.idx];
        return normalize(point - sph.center);
    }
    return vec3(0.0);
}

Hit traverseBVH(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    int closestN = -1;
    int stack[MAX_STACK_SIZE];
    int stackPtr = 0;
    stack[stackPtr++] = 0;
    while (stackPtr > 0) {
        int nodeIdx = stack[--stackPtr];
        Node node = nodes[nodeIdx];

        if (!intersectAABB(rayOri, rayDir, node.min, node.max)) continue;
        if (node.idx >= 0) {
            float t = intersect(rayOri, rayDir, node);
            if (t > 0.0 && t < closestT) {
                closestT = t;
                closestN = nodeIdx;
            }
        } else {
            if (node.rightIdx >= 0) stack[stackPtr++] = node.rightIdx;
            if (node.leftIdx >= 0)  stack[stackPtr++] = node.leftIdx;
        }
    }

    Hit hit;
    hit.t = closestT;
    hit.node = nodes[closestN];
    hit.mat = materials[hit.node.matIdx];
    if (hit.t != MAXILON) {
        hit.Q = rayOri + hit.t * rayDir;
        hit.N = getNormal(hit.Q, hit.node);
    }
    return hit;
}

vec4 getColor(vec3 rayOri, vec3 rayDir) {
    vec3 color = vec3(0.2);
    struct Ray {
        vec3 o;
        vec3 d;
    };

    Ray stack[MAX_STACK_SIZE];
    int stackPtr = 0;
    int reflections = 0;
    int refractions = 0;
    stack[stackPtr++] = Ray(rayOri, rayDir);
    while (stackPtr > 0) {
        Ray ray = stack[--stackPtr];
        Hit hit = traverseBVH(ray.o, ray.d);
        if (hit.t == MAXILON) continue;
        float diff = max(dot(hit.N, normalize(lightPos - hit.Q)), 0.0);
        color += (hit.mat.color * diff) * (hit.mat.reflectivity + hit.mat.translucency);
        if (hit.mat.emission > 0.0) {
            color += hit.mat.color * hit.mat.emission;
            continue;
        }
        vec3 nextOri = hit.Q + hit.N * MINSILON;
        if (reflections < MAX_REFLECTION_DEPTH && hit.mat.reflectivity > 0.0 && stackPtr < MAX_STACK_SIZE - 1) {
            stack[stackPtr++] = Ray(nextOri, reflect(ray.d, hit.N));
            reflections++;
        }
        if (refractions < MAX_REFRACTION_DEPTH && hit.mat.translucency > 0.0 && stackPtr < MAX_STACK_SIZE - 1) {
            stack[stackPtr++] = Ray(nextOri, refract(ray.d, hit.N, 1.0 / hit.mat.refractiveIndex));
            refractions++;
        }
    }

    return vec4(color, 1.0);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(imageSize(imgOutput));
    uv = uv * 2.0 - 1.0;
    uv.x *= aspect;
    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * normalize(cross(camForward, camUp)) + uv.y * tan(fov / 2.0) * camUp);
    int d = int(floor(distance(vec2(pixel), mousePos)));
    vec4 color = getColor(camPos, rayDir);
    if (d < MAX_RAY_DISTANCE) {
        int ray_density = int((MAX_RAY_DISTANCE - d) / MAX_RAY_DENSITY); //TODO: Handle this better
        ray_density = min(ray_density, 5);
        for (int y = -ray_density; y <= ray_density; y++) {
            for (int x = -ray_density; x <= ray_density; x++) {
                if (x == 0 && y == 0) continue;
                vec2 offset = vec2(float(x), float(y)) * 0.001;
                vec2 offsetUV = uv + offset;
                vec3 offsetRayDir = normalize(camForward + offsetUV.x * tan(fov / 2.0) * normalize(cross(camForward, camUp)) + offsetUV.y * tan(fov / 2.0) * camUp);
                color += getColor(camPos, offsetRayDir);
            }
        }
        float totalRays = float((2 * ray_density + 1) * (2 * ray_density + 1));
        color /= totalRays;
    }


    imageStore(imgOutput, pixel, color);
}
