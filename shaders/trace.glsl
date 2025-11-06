#version 430 core

struct Sphere {
    vec3 center;
    float radius;
    vec4 color;
    float emission;
    float reflectivity;
    float translucancy;
    float refractiveIndex;
};

struct Triangle {
    vec3 v0;
    float _pad0;
    vec3 v1;
    float _pad1;
    vec3 v2;
    float _pad2;
    vec4 color;
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

layout (std430, binding = 0) buffer Spheres { Sphere spheres[]; };

layout (std430, binding = 1) buffer Triangles { Triangle triangles[]; };

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(imgOutput);
    vec2 uv = (vec2(pixel) / vec2(size)) * 2.0 - 1.0;
    uv.x *= aspect;

    vec3 rayDir = normalize(camForward + uv.x * tan(fov / 2.0) * vec3(1.0, 0.0, 0.0) + uv.y * tan(fov / 2.0) * vec3(0.0, 1.0, 0.0));
    float closestT = 1e30;
    vec4 hitColor = vec4(0.0);

    for (int i = 0; i < spheres.length(); i++) {
        Sphere s = spheres[i];
        vec3 oc = camPos - s.center;
        float b = dot(oc, rayDir);
        float c = dot(oc, oc) - s.radius * s.radius;
        float discriminant = b * b - c;

        if (discriminant > 0.0) {
            float t = -b - sqrt(discriminant);
            if (t > 0.001 && t < closestT) {
                vec3 Q = camPos + t * rayDir;
                vec3 normal = normalize(Q - s.center);
                vec3 lightDir = normalize(Q - lightPos);
                float L = max(dot(normal, lightDir), 0.0);
                
                // reflection
                if (s.reflectivity > 0.0) {
                    vec3 reflectedDir = reflect(rayDir, normal);
                    //! TODO
                }
                
                
                closestT = t;
                hitColor = s.color * L * lightIntensity;
            }
        }
    }

    for (int i = 0; i < triangles.length(); i++) {
        Triangle tri = triangles[i];

        vec3 edge1 = tri.v1 - tri.v0;
        vec3 edge2 = tri.v2 - tri.v0;
        vec3 h = cross(rayDir, edge2);
        float a = dot(edge1, h);

        if (abs(a) > 0.0001) {
            float f = 1.0 / a;
            vec3 s = camPos - tri.v0;
            float u = f * dot(s, h);
            if (u >= 0.0 && u <= 1.0) {
                vec3 q = cross(s, edge1);
                float v = f * dot(rayDir, q);
                if (v >= 0.0 && u + v <= 1.0) {
                    float t = f * dot(edge2, q);
                    if (t > 0.001 && t < closestT) {
                        vec3 Q = camPos + t * rayDir;
                        vec3 normal = normalize(cross(tri.v0, tri.v1));
                        vec3 lightDir = normalize(Q - lightPos);
                        float L = max(dot(normal, lightDir), 0.0);

                        closestT = t;
                        hitColor = tri.color * L * lightIntensity;
                    }
                }
            }
        }
    }

    imageStore(imgOutput, ivec2(gl_GlobalInvocationID.xy), hitColor);
}
