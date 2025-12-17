#version 430 core

int DEPTH = 4;
const float EPSILON = 1e-6;
const float MINSILON = 1e-3;
const float MAXILON = 1e6;
const int MAX_STACK_SIZE = 64;
const float MAX_RAY_DISTANCE = 100.0;
const int EXTRA_RAYS = 2; // times 2 + 1 per axis

struct Triangle {
    vec4 d0; // v0.x, v0.y, v0.z, e1.x
    vec4 d1; // e1.y, e1.z, e2.x, e2.y
    vec4 d2; // e2.z, normal.x, normal.y, normal.z
};

vec3 v0(Triangle tri) { return tri.d0.xyz; };
vec3 e1(Triangle tri) { return vec3(tri.d0.w, tri.d1.x, tri.d1.y); };
vec3 e2(Triangle tri) { return vec3(tri.d1.z, tri.d1.w, tri.d2.x); };
vec3 n(Triangle  tri) { return tri.d2.yzw; };

struct Sphere { vec4 data0; };
vec3 center(Sphere sph) { return sph.data0.xyz; }
float radius(Sphere sph) { return sph.data0.w; }

struct Node {
    vec4 data0; // min.x, min.y, min.z, leftOrStart
    vec4 data1; // max.x, max.y, max.z, count
};

vec3 nmin(Node node) { return node.data0.xyz; }
vec3 nmax(Node node) { return node.data1.xyz; }
uint leftOrStart(Node node) { return floatBitsToUint(node.data0.w); }
uint count(Node node) { return floatBitsToUint(node.data1.w); }

struct Material {
    vec4 data0; // color.r, color.g, color.b, reflectivity
    vec4 data1; // translucency, emission, refractiveIndex, roughness
};

vec3 mColor(Material mat) { return mat.data0.rgb; }
float mRef(Material mat) { return mat.data0.a; }
float mTrans(Material mat) { return mat.data1.r; }
float mEmit(Material mat) { return mat.data1.g; }
float mRefrIdx(Material mat) { return mat.data1.b; }
float mRough(Material mat) { return mat.data1.a; }

struct Hit {
    float t;
    Node node;
    Material mat;
    vec3 Q;
    vec3 N;
};

struct Mesh {
    int matIdx;
    int bvhRoot;
    int triStart;
    int triCount;
};

struct TLAS {
    vec3 min; float _pad0;
    vec3 max; float _pad1;
    int idx;
    int type; // 0 = mesh, 1 = sphere
    int left;
    int right;
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

layout (std430, binding = 5) buffer Meshes { Mesh meshes[]; };

layout (std430, binding = 6) buffer TLASBuffer { TLAS tlas[]; };

float findTriangleIntersection(vec3 rayOrigin, vec3 rayDir, int i) {
    vec3 h = cross(rayDir, e2(triangles[i]));
    float a = dot(e1(triangles[i]), h);
    if (abs(a) < EPSILON) return MAXILON;
    float f = 1.0 / a;
    vec3 s = rayOrigin - v0(triangles[i]);
    float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return MAXILON;
    vec3 q = cross(s, e1(triangles[i]));
    float v = f * dot(rayDir, q);
    if (v < 0.0 || u + v > 1.0) return MAXILON;
    float t = f * dot(e2(triangles[i]), q);
    return (t > EPSILON) ? t : MAXILON;
}

float findSphereIntersection(vec3 rayOri, vec3 rayDir, int i) {
    vec3 oc = rayOri - center(spheres[i]);
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - pow(radius(spheres[i]), 2.0);
    float d = b * b - 4.0 * a * c;
    if (d < 0.0) return MAXILON;

    float sqrtD = sqrt(d);
    float inv = 1.0 / (2.0 * a);
    float t0 = (-b - sqrtD) * inv;
    if (t0 > EPSILON) return t0;

    float t1 = (-b + sqrtD) * inv;
    if (t1 > EPSILON) return t1;

    return MAXILON;
}

float intersectAABB(vec3 rayOri, vec3 invDir, vec3 minBound, vec3 maxBound) {
    vec3 tlow = (minBound - rayOri) * invDir;
    vec3 thigh = (maxBound - rayOri) * invDir;
    vec3 tmin = min(tlow, thigh);
    vec3 tmax = max(tlow, thigh);
    float tclose = max(max(tmin.x, tmin.y), tmin.z);
    float tfar = min(min(tmax.x, tmax.y), tmax.z);
    bool hit = tfar >= tclose && tfar >= 0.0;
    return hit ? tclose : MAXILON;
}

Hit traverseBVH(vec3 rayOri, vec3 rayDir, vec3 invRayDir, int meshIdx, float currentClosestT) {
    float closestT = currentClosestT;
    uint closestN = 0xFFFFFFFF;
    int closestTri = -1;

    uint istack[MAX_STACK_SIZE];
    float tstack[MAX_STACK_SIZE];
    int sp = 0;
    istack[sp] = meshes[meshIdx].bvhRoot;
    tstack[sp] = 0;
    sp++;

    while (sp-- > 0) {
        float t = tstack[sp];
        if (t >= closestT) continue;
        
        uint child = istack[sp];
        Node node = nodes[child];

        uint count = count(nodes[child]);
        if (count > 0) {
            uint start = leftOrStart(nodes[child]);
            for (uint i = start; i < start + count; i++) {
                int triIndex = triIndices[i];
                float t = findTriangleIntersection(rayOri, rayDir, triIndex);
                if (t > 0.0 && t < closestT) {
                    closestT = t;
                    closestTri = triIndex;
                    closestN = child;
                }
            }
            continue;
        } 

        uint left = leftOrStart(nodes[child]);
        uint right = left + 1u;

        float tL = intersectAABB(rayOri, invRayDir, nmin(nodes[left]), nmax(nodes[left]));
        float tR = intersectAABB(rayOri, invRayDir, nmin(nodes[right]), nmax(nodes[right]));

        if (tL < tR) {
            if (tR <= closestT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = right; 
                tstack[sp] = tR; 
                sp++; 
            }
            if (tL <= closestT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = left;  
                tstack[sp] = tL; 
                sp++; 
            }
        } else {
            if (tL < closestT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = left;  
                tstack[sp] = tL; 
                sp++; 
            }
            if (tR < closestT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = right; 
                tstack[sp] = tR; 
                sp++; 
            }
        }
    }

    if (closestN == 0xFFFFFFFFu || closestT == MAXILON) {
        Hit noHit;
        noHit.t = MAXILON;
        return noHit;
    }

    Hit hit;
    hit.t = closestT;
    hit.node = nodes[closestN];
    hit.Q = rayOri + hit.t * rayDir;
    hit.N = n(triangles[closestTri]);
    return hit;
}

bool traverseBVHAny(vec3 rayOri, vec3 rayDir, vec3 invRayDir, int meshIdx, float maxT) {
    float closestT = maxT;

    uint istack[MAX_STACK_SIZE];
    float tstack[MAX_STACK_SIZE];
    int sp = 0;
    istack[sp] = meshes[meshIdx].bvhRoot;
    tstack[sp] = 0;
    sp++;

    while (sp-- > 0) {
        float t = tstack[sp];
        if (t >= closestT) continue;
        
        uint child = istack[sp];

        uint count = count(nodes[child]);
        if (count > 0) {
            uint start = leftOrStart(nodes[child]);
            for (uint i = start; i < start + count; i++) {
                int triIndex = triIndices[i];
                float t = findTriangleIntersection(rayOri, rayDir, triIndex);
                if (t > 0.0 && t < closestT) return true;
            }
            continue;
        } 

        uint left = leftOrStart(nodes[child]);
        uint right = left + 1u;

        float tL = intersectAABB(rayOri, invRayDir, nmin(nodes[left]), nmax(nodes[left]));
        float tR = intersectAABB(rayOri, invRayDir, nmin(nodes[right]), nmax(nodes[right]));

        if (tL < tR) {
            if (tR <= closestT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = right; 
                tstack[sp] = tR; 
                sp++; 
            }
            if (tL <= closestT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = left;  
                tstack[sp] = tL; 
                sp++; 
            }
        } else {
            if (tL < closestT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = left;  
                tstack[sp] = tL; 
                sp++; 
            }
            if (tR < closestT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                istack[sp] = right; 
                tstack[sp] = tR; 
                sp++; 
            }
        }
    }
    return false;
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
    hit.N = normalize(hit.Q - center(spheres[closestIdx]));
    return hit;
}

Hit getHit(vec3 rayOri, vec3 rayDir) {
    vec3 invRayDir = 1.0 / rayDir;
    float closestT = MAXILON;
    Hit finalHit;
    finalHit.t = MAXILON;

    uint istack[MAX_STACK_SIZE];
    float tstack[MAX_STACK_SIZE];
    int sp = 0;
    istack[sp] = 0;
    tstack[sp] = 0;
    sp++;

    while (sp-- > 0) {
        float t = tstack[sp];
        if (t >= closestT) continue;
        
        uint child = istack[sp];

        if (tlas[child].type == 0) {
            Hit hit = traverseBVH(rayOri, rayDir, invRayDir, tlas[child].idx, closestT);
            if (hit.t < closestT) {
                closestT = hit.t;
                finalHit = hit;
                finalHit.mat = materials[meshes[tlas[child].idx].matIdx];
            }
        } else if (tlas[child].type == 1) {
            Hit hit = intersectSpheres(rayOri, rayDir);
            if (hit.t < closestT) {
                closestT = hit.t;
                finalHit = hit;
                finalHit.mat = materials[1];
            }
        } else {
            uint left = uint(tlas[child].left);
            uint right = uint(tlas[child].right);

            TLAS ln = tlas[left];
            TLAS rn = tlas[right];

            float tL = intersectAABB(rayOri, invRayDir, ln.min, ln.max);
            float tR = intersectAABB(rayOri, invRayDir, rn.min, rn.max);

            if (tL < tR) {
                if (tR <= closestT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = right; 
                    tstack[sp] = tR; 
                    sp++; 
                }
                if (tL <= closestT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = left;  
                    tstack[sp] = tL; 
                    sp++; 
                }
            } else {
                if (tL < closestT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = left;  
                    tstack[sp] = tL; 
                    sp++;
                }
                if (tR < closestT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = right; 
                    tstack[sp] = tR; 
                    sp++; 
                }
            }
        }
    }

    return finalHit;
}

bool getHitAny(vec3 rayOri, vec3 rayDir, float maxT) {
    vec3 invRayDir = 1.0 / rayDir;

    uint istack[MAX_STACK_SIZE];
    float tstack[MAX_STACK_SIZE];
    int sp = 0;
    istack[sp] = 0;
    tstack[sp] = 0;
    sp++;

    while (sp-- > 0) {
        float t = tstack[sp];
        if (t >= maxT) continue;
        
        uint child = istack[sp];

        if (tlas[child].type == 0) {
            if (traverseBVHAny(rayOri, rayDir, invRayDir, tlas[child].idx, maxT)) {
                return true;
            }
        } else if (tlas[child].type == 1) {
            Hit hit = intersectSpheres(rayOri, rayDir);
            if (hit.t < maxT) {
                return true;
            }
        } else {
            uint left = uint(tlas[child].left);
            uint right = uint(tlas[child].right);

            TLAS ln = tlas[left];
            TLAS rn = tlas[right];

            float tL = intersectAABB(rayOri, invRayDir, ln.min, ln.max);
            float tR = intersectAABB(rayOri, invRayDir, rn.min, rn.max);

            if (tL < tR) {
                if (tR <= maxT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = right; 
                    tstack[sp] = tR; 
                    sp++; 
                }
                if (tL <= maxT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = left;  
                    tstack[sp] = tL; 
                    sp++; 
                }
            } else {
                if (tL < maxT && tL != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = left;  
                    tstack[sp] = tL; 
                    sp++; 
                }
                if (tR < maxT && tR != MAXILON && sp < MAX_STACK_SIZE) { 
                    istack[sp] = right; 
                    tstack[sp] = tR; 
                    sp++;
                }
            }
        }
    }
    return false;
}

float rand01(inout uint rng) {
    rng = rng * 1664525u + 1013904223u;
    return float(rng) / 4294967295.0;
}
    
vec4 getColorRay(vec3 rayOri, vec3 rayDir) {
    vec3 color = vec3(0.0);
    struct Ray { vec3 ori; vec3 dir; vec3 inv; };

    int depth = 0;
    Ray stack[MAX_STACK_SIZE];
    int sp = 0;
    stack[sp++] = Ray(rayOri, rayDir, 1.0 / rayDir);
    while (sp > 0 && depth < DEPTH) {
        Ray ray = stack[--sp];
        Hit hit = getHit(ray.ori, ray.dir);
        if (hit.t >= MAXILON) continue;

        vec3 N = faceforward(hit.N, ray.dir, hit.N);

        if (mTrans(hit.mat) > 0.0) {
            bool entering = dot(N, ray.dir) < 0.0;
            vec3 Ntrans = entering ? N : -N;
            float eta = entering ? (1.0 / mRefrIdx(hit.mat)) : mRefrIdx(hit.mat);
            vec3 rd = refract(normalize(ray.dir), Ntrans, eta);
            if (length(rd) > 0.0) {
                vec3 offset = entering ? (-Ntrans * MINSILON) : (Ntrans * MINSILON);
                rd = normalize(rd);
                stack[sp++] = Ray(hit.Q + offset, rd, 1.0 / rd);
                depth++;
            }
        } 
        
        vec3 biased = hit.Q + N * MINSILON;
        if (mRef(hit.mat) > 0.0) {
            vec3 rd = reflect(normalize(ray.dir), N);
            stack[sp++] = Ray(biased, rd, 1.0 / rd);
            depth++;
        }

        vec3 lightDir = normalize(lightPos - hit.Q);
        vec3 diffuse = abs(dot(N, lightDir)) * mColor(hit.mat);
        
        if (mEmit(hit.mat) > 0.0) {
            color += mColor(hit.mat) * mEmit(hit.mat);
        }

        float lightDist = length(lightPos - biased);

        if (getHitAny(biased, lightDir, lightDist)) {
            Hit shadowHit = getHit(biased, lightDir);

            float ambient = 0.15;
            const int shadowHelpers = 16;
            int shadowHits = 0;

            uint shadowRng = uint(gl_GlobalInvocationID.x) * 1973u
                           ^ uint(gl_GlobalInvocationID.y) * 9277u
                           //^ uint(time) * 2663u // only useful for accumulating frames
                           ^ 0x9E3779B9u;

            vec3 up = (abs(lightDir.y) < 0.99) ? vec3(0, 1, 0) : vec3(1, 0, 0);
            vec3 right = normalize(cross(lightDir, up));
            up = normalize(cross(right, lightDir));

            float radius = 0.2;
            for (int k = 0; k < shadowHelpers; k++) {
                float baseAngle = (2.0 * 3.14159265359) * (float(k) / float(shadowHelpers));
                float angJ = (rand01(shadowRng) - 0.5) * (2.0 * 3.14159265359 / float(shadowHelpers));
                float radJ = 0.35 + 0.65 * rand01(shadowRng);

                float a = baseAngle + angJ;
                vec2 o2 = vec2(cos(a), sin(a)) * (radius * radJ);
                o2 += vec2(rand01(shadowRng) - 0.5, rand01(shadowRng) - 0.5) * (radius * 0.15);

                vec3 offset = right * o2.x + up * o2.y;
                vec3 helperOri = biased + offset;
                if (getHitAny(helperOri, lightDir, lightDist)) shadowHits++;
            }

            ambient += pow(1.0 - float(shadowHits) / float(shadowHelpers), 2.0);
            if (mTrans(shadowHit.mat) > 0.0) ambient += mTrans(shadowHit.mat);
            color += diffuse * clamp(ambient, 0.0, 1.0);
            continue;
        }
        
        color += diffuse * (1.0 - mRef(hit.mat) - mTrans(hit.mat));
    }
    return vec4(color, 1.0);
}

vec3 addJitter(vec3 dir, inout uint rng) {
    float u1 = rand01(rng);
    float u2 = rand01(rng);

    float r = sqrt(u1);
    float phi = 2.0 * 3.14159265359 * u2;

    vec3 w = normalize(dir);
    vec3 a = (abs(w.z) < 0.999) ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 u = normalize(cross(a, w));
    vec3 v = cross(w, u);

    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1));

    return normalize(x * u + y * v + z * w);
}

vec3 addConeJitter(vec3 dir, float coneAngleRad, inout uint rng) {
    vec3 w = normalize(dir);

    float ca = clamp(coneAngleRad * 0.1, 0.0, 1.57079632679);
    if (ca <= 0.0) return w;

    float u1 = rand01(rng);
    float u2 = rand01(rng);

    float cosMax = cos(ca);
    float cosTheta = mix(1.0, cosMax, u1);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * 3.14159265359 * u2;

    vec3 a = (abs(w.z) < 0.999) ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 u = normalize(cross(a, w));
    vec3 v = cross(w, u);

    vec3 d = (cos(phi) * sinTheta) * u + (sin(phi) * sinTheta) * v + cosTheta * w;
    return normalize(d);
}

vec4 getColorPath(vec3 rayOri, vec3 rayDir, vec3 prevColor, int frames) {
    vec3 collectedLight = vec3(0.0);

    uvec3 gid = gl_GlobalInvocationID;
    uint rng = uint(gid.x) * 1973u ^ uint(gid.y) * 9277u ^ uint(gid.z) * 2663u ^ 0x9E3779B9u;
    rng += uint(frames) * 1013904223u;

    vec3 throughput = vec3(1.0);

    for (int bounce = 0; bounce < DEPTH; bounce++) {
        Hit hit = getHit(rayOri, rayDir);
        if (hit.t == MAXILON || length(throughput) <= MINSILON) break;

        if (mEmit(hit.mat) > 0.0) {
            collectedLight += throughput * mColor(hit.mat) * mEmit(hit.mat);
            break;
        }

        vec3 Ng = normalize(hit.N);
        bool entering = (dot(Ng, rayDir) < 0.0);
        vec3 N = entering ? Ng : -Ng;
        float r = rand01(rng);

        rayOri = hit.Q + N * MINSILON;

        vec3 L = lightPos - rayOri;
        float dist2 = dot(L, L);
        float dist = sqrt(dist2);
        vec3 ldir = L / dist;
        float ndotl = max(dot(N, ldir), 0.0);
        if (ndotl > 0.0) {
            collectedLight += throughput * mColor(hit.mat) * (lightIntensity * ndotl / max(dist2, EPSILON));
            //if (!getHitAny(rayOri, ldir, dist - MINSILON)) {
            //}
        }

        if (mRef(hit.mat) > r) {
            rayDir = addConeJitter(reflect(rayDir, N), mRough(hit.mat), rng);
        } else if (mTrans(hit.mat) + mRef(hit.mat) > r) {
            float eta = entering ? (1.0 / mRefrIdx(hit.mat)) : mRefrIdx(hit.mat);
            vec3 rd = refract(normalize(rayDir), N, eta);
            if (length(rd) > 0.0) {
                rayDir = addConeJitter(normalize(rd), mRough(hit.mat), rng);
                rayOri = hit.Q - N * MINSILON;
            } else {
                rayDir = addConeJitter(reflect(rayDir, N), mRough(hit.mat), rng);
            }
        } else {
            rayDir = addJitter(N, rng);
        }

        throughput *= mColor(hit.mat);
    }

    vec3 finalColor = mix(prevColor, collectedLight, 1.0 / float(frames + 1));
    return vec4(finalColor, 1.0);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(imageSize(imgOutput));
    uv = uv * 2.0 - 1.0;
    uv.x *= aspect;
    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * normalize(cross(camForward, camUp)) + uv.y * tan(fov / 2.0) * camUp);
    float d = distance(vec2(pixel), mousePos);
    
    vec4 color = getColorRay(camPos, rayDir);
    if (d < MAX_RAY_DISTANCE) {
        DEPTH = 6;
        for (int y = -EXTRA_RAYS; y <= EXTRA_RAYS; y++) {
            for (int x = -EXTRA_RAYS; x <= EXTRA_RAYS; x++) {
                if (x == 0 && y == 0) continue;
                vec3 offset = vec3(float(x), float(y), 0.0) * 0.005;
                vec4 jitteredColor = getColorRay(camPos + offset, rayDir);
                color += jitteredColor;
            }
        }
        int rays = (EXTRA_RAYS * 2 + 1) * (EXTRA_RAYS * 2 + 1);
        color.rgb /= float(rays);
    }
    imageStore(imgOutput, pixel, color);

    //DEPTH = 32;
    //vec4 prevColor = imageLoad(imgOutput, pixel);
    //vec4 color = getColorPath(camPos, rayDir, prevColor.rgb, time);
    //imageStore(imgOutput, pixel, color);
}
