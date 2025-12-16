#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <utilities.hh>
#include <structs.hh>
#include <bvh.hh>

#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

using namespace glm;
using namespace std;

int WIDTH = Config::width;
int HEIGHT = Config::height;

void generate_scene() {
    vector<Mesh> suz = createObjectFromFile("../models/suzanne.obj");
    mat4 suzTransform = get_translation(vec3(-1.75f, 1.8f, 0.0f)) *
                        get_rotation_y(radians(10.0f)) *
                        get_rotation_x(radians(-30.0f));
    transform(suz, suzTransform);
    buildBVHs(suz);
    meshes.insert(meshes.end(), suz.begin(), suz.end());

    vector<Mesh> box = createObjectFromFile("../models/cornell-box.obj");
    mat4 boxTransform = get_translation(vec3(0.4f, -5.0f, 8.0f)) *
                        get_scaling(2.0f);
    transform(box, boxTransform);
    buildBVHs(box);
    meshes.insert(meshes.end(), box.begin(), box.end());

    vector<Mesh> spot = createObjectFromFile("../models/spot.obj");
    mat4 spotTransform = get_translation(vec3(1.2f, -1.3f, 4.2f)) *
                        get_rotation_y(radians(130.0f));
    transform(spot, spotTransform);
    buildBVHs(spot);
    meshes.insert(meshes.end(), spot.begin(), spot.end());

    const float s = 5.0f;
    const float ts = 1.0f;
    srand(69);
    int start = static_cast<int>(triangles.size());
    for (int i = 0; i < Config::Num; i++) {
        Tri tri;
        vec3 j0 = vec3(rnd(-s, s), rnd(-s, s), rnd(-s, s));
        vec3 j1 = vec3(rnd(-ts, ts), rnd(-ts, ts), rnd(-ts, ts));
        vec3 j2 = vec3(rnd(-ts, ts), rnd(-ts, ts), rnd(-ts, ts));
        tri.v0 = vec3(j0);
        tri.v1 = vec3(j0 + j1);
        tri.v2 = vec3(j0 + j2);
        tri.min = min(tri.v0, min(tri.v1, tri.v2));
        tri.max = max(tri.v0, max(tri.v1, tri.v2));
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        tri.normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
        tri.materialIdx = 2;
        triangles.push_back(tri);
        triIndices.push_back(static_cast<int>(triangles.size() - 1));
        Sph sph;
        sph.center = vec3(rnd(-s, s), rnd(-s, s), rnd(-s, s));
        sph.radius = rnd(0.1f, 0.8f);
        sph.materialIdx = rnd(0, 1) > 0.5f ? materialMap["BloodyRed"] : materialMap["DarkGreen"];
        spheres.push_back(sph);
    }
    Mesh randomMesh;
    randomMesh.materialIdx = materialMap["DarkGreen"];
    randomMesh.triStart = start;
    randomMesh.triCount = Config::Num;
    randomMesh.bvhRoot = -1;
    buildBVH(randomMesh);
    meshes.push_back(randomMesh);
}

void init(GLuint triSSBO, GLuint sphSSBO, GLuint bvhSSBO, GLuint triIndSSBO, GLuint meshSSBO) {
    generate_scene();

    vector<GPUTri> gpuTris;
    for (Tri& tri : triangles) {
        vec3 e1 = tri.v1 - tri.v0;
        vec3 e2 = tri.v2 - tri.v0;

        GPUTri gtri;
        gtri.data0 = vec4(tri.v0, e1.x);
        gtri.data1 = vec4(e1.y, e1.z, e2.x, e2.y);
        gtri.data2 = vec4(e2.z, tri.normal);
        gpuTris.push_back(gtri);
    }

    vector<GPUSph> gpuSphs;
    for (Sph& sph : spheres) {
        GPUSph gsph;
        gsph.data0 = vec4(sph.center, sph.radius);
        gpuSphs.push_back(gsph);
    }

    vector<GPUNode> gpuNodes;
    for (Node& node : nodes) {
        GPUNode gnode;
        gnode.data0 = vec4(node.min, (node.count == 0) ? static_cast<float>(node.left) : static_cast<float>(node.start));
        gnode.data1 = vec4(node.max, static_cast<float>(node.count));
        gpuNodes.push_back(gnode);
    }

    cout << "Memory Usage:\n"
         << " - Triangle size: " << (gpuTris.size() * sizeof(GPUTri)) / 1000000.0 << " MB" << "\n"
         << " - Sphere size: " << (gpuSphs.size() * sizeof(GPUSph)) / 1000000.0 << " MB" << "\n"
         << " - BVH size: " << (gpuNodes.size() * sizeof(GPUNode)) / 1000000.0 << " MB" << "\n"
         << "Total Amounts:\n"
         << " - triangles: " << triangles.size() << "\n"
         << " - spheres: " << spheres.size() << "\n"
         << " - BVH nodes: " << nodes.size() << "\n";

    createAndFillSSBO<GPUTri>(triSSBO, 0, gpuTris);
    createAndFillSSBO<GPUSph>(sphSSBO, 1, gpuSphs);
    createAndFillSSBO<GPUNode>(bvhSSBO, 2, gpuNodes);
    createAndFillSSBO<int>(triIndSSBO, 4, triIndices);
    createAndFillSSBO<Mesh>(meshSSBO, 5, meshes);
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Raytracer", nullptr, nullptr);
    if (!window) {
        cerr << "Failed to create window\n";
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Failed to init GLAD\n";
        return -1;
    }
    glfwSwapInterval(0);

    cout << "GL_VENDOR:   " << glGetString(GL_VENDOR)   << "\n";
    cout << "GL_RENDERER: " << glGetString(GL_RENDERER) << "\n";
    cout << "GL_VERSION:  " << glGetString(GL_VERSION)  << "\n";

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, WIDTH, HEIGHT);
    glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    GLuint computeProgram = createProgram("../shaders/trace.glsl");

    float triVertices[] = { -1.0f, -1.0f,  3.0f, -1.0f, -1.0f,  3.0f };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triVertices), triVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    GLuint quadProgram = createQuadProgram("../shaders/quad.vert", "../shaders/quad.frag");

    GLuint cameraUBO;
    Camera cam;
    createCamera(cameraUBO, cam, WIDTH, HEIGHT);
    createLights();

    vec2 mousePos = vec2(0.0f);
    GLuint mouseUBO;
    createAndFillUBO<vec2>(mouseUBO, 2, mousePos);

    GLuint triSSBO, sphSSBO, bvhSSBO, materialSSBO, triIndSSBO, meshSSBO;
    createAndFillSSBO<Material>(materialSSBO, 3, materials);

    float initialTime = glfwGetTime();
    init(triSSBO, sphSSBO, bvhSSBO, triIndSSBO, meshSSBO);
    cout << "BVH build time: " << (glfwGetTime() - initialTime) << " seconds\n";
    
    int nbFrames = 0;
    int totalFrames = 0;
    double lastTime = glfwGetTime();
    double currentTime = lastTime;
    GLuint timeUBO;
    createAndFillUBO<int>(timeUBO, 3, totalFrames);

    double lastFrameTime = 0.0f;
    int width, height;
    const int dispatchSize = 8;
    const GLuint gx = (WIDTH + dispatchSize - 1) / dispatchSize;
    const GLuint gy = (HEIGHT + dispatchSize - 1) / dispatchSize;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        glUseProgram(computeProgram);
        glDispatchCompute(gx, gy, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(quadProgram);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        nbFrames++;
        totalFrames++;
        currentTime = glfwGetTime();
        updateUBO<int>(timeUBO, totalFrames);

        double deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        if (processInput(window, &cam, deltaTime)) {
            updateUBO<Camera>(cameraUBO, cam);
            totalFrames = 0;
        }

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        if (xpos != mousePos.x || ypos != HEIGHT - mousePos.y) {
            mousePos = vec2(xpos, HEIGHT - ypos);
            updateUBO<vec2>(mouseUBO, mousePos);
        }

        if (currentTime - lastTime >= 1.0) {
            double fps = double(nbFrames) / (currentTime - lastTime);
            double frameTimeMs = 1000.0 / fps;

            stringstream title;
            title << "Raytracer - " << fps << " FPS (" << frameTimeMs << " ms/frame)";
            glfwSetWindowTitle(window, title.str().c_str());

            nbFrames = 1;
            lastTime = currentTime;
        }   
        glfwSwapBuffers(window);
    }

    glDeleteProgram(computeProgram);
    glDeleteProgram(quadProgram);
    glDeleteTextures(1, &tex);
    glfwTerminate();
    return 0;
}