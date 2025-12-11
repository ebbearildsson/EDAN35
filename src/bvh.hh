#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <structs.hh>

void buildNode(int idx, vec3 minv, vec3 maxv, std::vector<Type>& idxs);

void tightenBounds(int index);