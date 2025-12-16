#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <structs.hh>

void buildBVH(Mesh& mesh);

void buildBVHs(std::vector<Mesh>& meshes);