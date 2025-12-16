#include <structs.hh>
#include <vector>

std::vector<int> triIndices;
std::vector<Tri> triangles;
std::vector<Mesh> meshes;
std::vector<Sph> spheres;
std::vector<Node> nodes;

std::vector<Material> getMaterials() {
    std::vector<Material> tempMaterials;
    
    Material defaultMat;
    defaultMat.color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
    defaultMat.reflectivity = 0.0f;
    defaultMat.translucency = 0.0f;
    defaultMat.emission = 0.0f;
    defaultMat.refractiveIndex = 1.0f;
    tempMaterials.push_back(defaultMat);
    
    Material reflectiveRed;
    reflectiveRed.color = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    reflectiveRed.reflectivity = 0.6f;
    reflectiveRed.translucency = 0.0f;
    reflectiveRed.emission = 0.0f;
    reflectiveRed.refractiveIndex = 1.0f;
    tempMaterials.push_back(reflectiveRed);
    
    Material translucentBlue;
    translucentBlue.color = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    translucentBlue.reflectivity = 0.2f;
    translucentBlue.translucency = 0.6f;
    translucentBlue.emission = 0.0f;
    translucentBlue.refractiveIndex = 1.5f;
    tempMaterials.push_back(translucentBlue);
    
    Material emissiveGreen;
    emissiveGreen.color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
    emissiveGreen.reflectivity = 0.0f;
    emissiveGreen.translucency = 0.0f;
    emissiveGreen.emission = 0.5f;
    emissiveGreen.refractiveIndex = 1.0f;
    tempMaterials.push_back(emissiveGreen);
    
    Material emissivePurple;
    emissivePurple.color = vec4(0.5f, 0.0f, 0.5f, 1.0f);
    emissivePurple.reflectivity = 0.0f;
    emissivePurple.translucency = 0.0f;
    emissivePurple.emission = 1.0f;
    emissivePurple.refractiveIndex = 1.0f;
    tempMaterials.push_back(emissivePurple);
    
    return tempMaterials;
}

std::unordered_map<std::string,int> materialMap = []{
    std::unordered_map<std::string,int> m;
    m["Khaki"] = 0;
    m["BloodyRed"] = 1;
    m["DarkGreen"] = 2;
    m["Light"] = 3;
    m["Purple"] = 4;
    return m;
}();

std::vector<Material> materials = getMaterials();