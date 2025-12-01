#version 430 core

const float EPSILON = 1e-6;
const float MAXILON = 1e6;
const int MAX_STACK_SIZE = 128;
const int MAX_REFLECTION_DEPTH = 5;

struct Triangle { // 64 bytes
    vec3 v0; float _pad0;
    vec3 v1; float _pad1;
    vec3 v2; float _pad2;
    vec3 c;  float _pad3;
};

struct Sphere { // 16 bytes
    vec3 center;
    float radius;
};

struct Node { // 48 bytes
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
    vec4 color;
    float reflectivity;
    float translucency;
    float emission;
    float refractiveIndex;
};

struct Hit {
    float t;
    int nodeIdx;
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
    float t1 = (-b + sqrtD) / (2.0 * a);

    if (t0 > EPSILON) return t0;
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

float intersect(vec3 rayOri, vec3 rayDir, Node n) {
    if (n.type == 0) {
        return findTriangleIntersection(rayOri, rayDir, n.idx);
    } else if (n.type == 1) {
        return findSphereIntersection(rayOri, rayDir, n.idx);
    }
    return MAXILON;
}

Hit traverseBVH(vec3 rayOri, vec3 rayDir) {
    float closestT = MAXILON;
    int closestN = -1;
    int stack[128];
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
    hit.nodeIdx = closestN;
    return hit;
}

vec3 getNormal(vec3 point, Node node) {
    if (node.type == 0) {
        Triangle tri = triangles[node.idx];
        return normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    } else if (node.type == 1) {
        Sphere sph = spheres[node.idx];
        return normalize(point - sph.center);
    }
    return vec3(0.0);
}

vec4 getColor(vec3 rayOri, vec3 rayDir) {
    Hit hit = traverseBVH(rayOri, rayDir);
    if (hit.t == MAXILON) return vec4(0.2);
    Node node = nodes[hit.nodeIdx];
    Material mat = materials[node.matIdx];

    vec3 Q = rayOri + hit.t * rayDir;
    vec3 N = getNormal(Q, node);
    vec3 biasQ = Q + N * 1e-3;


    float diff = max(dot(N, normalize(lightPos - Q)), 0.0);
    vec3 color = mat.color.rgb * diff;

    //TODO: reflections
    if (mat.reflectivity > 0.0) {
        vec3 currDir = reflect(rayDir, N);
        vec3 currOri = biasQ;
        vec3 accumColor = vec3(0.0);
        for (int bounce = 0; bounce < MAX_REFLECTION_DEPTH; bounce++) {
            Hit reflectHit = traverseBVH(currOri, currDir);
            if (reflectHit.t == MAXILON) break;
            vec3 currQ = currOri + reflectHit.t * currDir;
            Node reflectNode = nodes[reflectHit.nodeIdx];
            Material currMat = materials[reflectNode.matIdx];
            vec3 currN = getNormal(currQ, reflectNode);
            float currDiff = max(dot(currN, normalize(lightPos - currQ)), 0.0);
            currOri = currQ + currN * 1e-3;
            accumColor += (currMat.color.rgb * pow(currMat.reflectivity, bounce + 2)) * currDiff;
            if (currMat.reflectivity <= 0.0) break;
            currDir = reflect(currDir, currN);
        }
        color += accumColor;
    }

    //TODO: refractions

    //TODO: emission
    color += mat.color.rgb * mat.emission;

    return vec4(color, 1.0);
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
