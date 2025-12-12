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

void generate_scene(vector<Type>& indices, int& ind) {
    vector<Tri> suz = createObjectFromFile("../models/suzanne.obj", materialMap);
    rotate_object_x(suz, radians(-30.0f));
    rotate_object_y(suz, radians(10.0f));
    scale_object(suz, 1.0f);
    translate_object(suz, vec3(-1.75f, 1.8f, 0.0f));
    add_object(suz, indices, ind);

    vector<Tri> box = createObjectFromFile("../models/cornell-box.obj", materialMap);
    scale_object(box, 2.0f);
    translate_object(box, vec3(0.4f, -5.0f, 8.0f));
    add_object(box, indices, ind);

    vector<Tri> spot = createObjectFromFile("../models/spot.obj", materialMap);
    rotate_object_y(spot, radians(130.0f));
    scale_object(spot, 1.0f);
    translate_object(spot, vec3(1.2f, -1.3f, 4.2f));
    add_object(spot, indices, ind);


    const float s = 5.0f;
    const float ts = 1.0f;
    srand(69);
    for (int i = 0; i < Config::Num; i++) {
        Tri tri;
        vec3 j0 = vec3(rnd(-s, s), rnd(-s, s), rnd(-s, s));
        vec3 j1 = vec3(rnd(-ts, ts), rnd(-ts, ts), rnd(-ts, ts));
        vec3 j2 = vec3(rnd(-ts, ts), rnd(-ts, ts), rnd(-ts, ts));
        tri.v0 = vec3(j0);
        tri.v1 = vec3(j0 + j1);
        tri.v2 = vec3(j0 + j2);
        tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        tri.normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
        tri.materialIdx = 2;
        triangles.push_back(tri);
        indices.push_back({ind, 0});
        ind++;
        Sph sph;
        sph.center = vec3(rnd(-s, s), rnd(-s, s), rnd(-s, s));
        sph.radius = rnd(0.1f, 0.8f);
        sph.materialIdx = 1;
        spheres.push_back(sph);
        indices.push_back({i, 1});
    }
}

void init(GLuint triSSBO, GLuint sphSSBO, GLuint bvhSSBO) {
    vector<Type> allIndices;
    int ind = 0;
    generate_scene(allIndices, ind);
    
    buildNode(0, vec3(-10.0f), vec3(10.0f), allIndices);
    tightenBounds(0); //? Probably not needed if bounds are calculated correctly during build
    
    vector<GPUTri> gpuTris;
    for (Tri& tri : triangles) {
        GPUTri gtri;
        gtri.v0 = vec4(tri.v0, 1.0f);
        gtri.v1 = vec4(tri.v1, 1.0f);
        gtri.v2 = vec4(tri.v2, 1.0f);
        gtri.n = vec4(tri.normal, 0.0f);
        gpuTris.push_back(gtri);
    }
    vector<GPUSph> gpuSphs;
    for (Sph& sph : spheres) {
        GPUSph gsph;
        gsph.center = sph.center;
        gsph.radius = sph.radius;
        gpuSphs.push_back(gsph);
    }

    cout << "Memory Usage:\n"
         << " - Triangle size: " << (gpuTris.size() * sizeof(GPUTri)) / 1000000.0 << " MB" << "\n"
         << " - Sphere size: " << (gpuSphs.size() * sizeof(GPUSph)) / 1000000.0 << " MB" << "\n"
         << " - BVH size: " << (nodes.size() * sizeof(Node)) / 1000000.0 << " MB" << "\n"
         << "Total Amounts:\n"
         << " - triangles: " << triangles.size() << "\n"
         << " - spheres: " << spheres.size() << "\n"
         << " - BVH nodes: " << nodes.size() << "\n";

    createAndFillSSBO<GPUTri>(triSSBO, 0, gpuTris);
    createAndFillSSBO<GPUSph>(sphSSBO, 1, gpuSphs);
    createAndFillSSBO<Node>(bvhSSBO, 2, nodes);
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

    Material defaultMat;
    defaultMat.color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
    defaultMat.reflectivity = 0.0f;
    defaultMat.translucency = 0.0f;
    defaultMat.emission = 0.0f;
    defaultMat.refractiveIndex = 1.0f;
    materials.push_back(defaultMat);

    Material reflectiveRed;
    reflectiveRed.color = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    reflectiveRed.reflectivity = 0.6f;
    reflectiveRed.translucency = 0.0f;
    reflectiveRed.emission = 0.0f;
    reflectiveRed.refractiveIndex = 1.0f;
    materials.push_back(reflectiveRed);

    Material translucentBlue;
    translucentBlue.color = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    translucentBlue.reflectivity = 0.2f;
    translucentBlue.translucency = 0.6f;
    translucentBlue.emission = 0.0f;
    translucentBlue.refractiveIndex = 1.5f;
    materials.push_back(translucentBlue);

    Material emissiveGreen;
    emissiveGreen.color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
    emissiveGreen.reflectivity = 0.0f;
    emissiveGreen.translucency = 0.0f;
    emissiveGreen.emission = 0.5f;
    emissiveGreen.refractiveIndex = 1.0f;
    materials.push_back(emissiveGreen);

    Material emissivePurple;
    emissivePurple.color = vec4(0.5f, 0.0f, 0.5f, 1.0f);
    emissivePurple.reflectivity = 0.0f;
    emissivePurple.translucency = 0.0f;
    emissivePurple.emission = 1.0f;
    emissivePurple.refractiveIndex = 1.0f;
    materials.push_back(emissivePurple);

    materialMap["Khaki"] = 0;
    materialMap["BloodyRed"] = 1;
    materialMap["DarkGreen"] = 2;
    materialMap["Light"] = 3;
    materialMap["Purple"] = 4;

    GLuint triSSBO, sphSSBO, bvhSSBO, materialSSBO;
    createAndFillSSBO<Material>(materialSSBO, 3, materials);

    float initialTime = glfwGetTime();
    init(triSSBO, sphSSBO, bvhSSBO);
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