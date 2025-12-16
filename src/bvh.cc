#include <glm/glm.hpp>
#include <structs.hh>
#include <bvh.hh>

#include <algorithm>
#include <vector>

void shrinkBounds(int nodeIdx) {
    Node* node = &nodes[nodeIdx];
    node->min = vec3(FLT_MAX);
    node->max = vec3(-FLT_MAX);
    int start = node->start;
    int end   = node->start + node->count;
    for (int i = start; i < end; i++) {
        Tri& tri = triangles[triIndices[i]];
        node->min = min(node->min, tri.min);
        node->max = max(node->max, tri.max);
    }
}

void swap(int& a, int& b) {
    int temp = a;
    a = b;
    b = temp;
}

float area(vec3 minv, vec3 maxv) {
    vec3 e = maxv - minv;
    return 2 * (e.x * e.y + e.y * e.z + e.z * e.x);
}

float evalSAH( Node node, int axis, float splitPos ) {
    struct Bounds {
        vec3 min;
        vec3 max;
        int count;
    };

    Bounds left;
    left.min = vec3(FLT_MAX);
    left.max = vec3(-FLT_MAX);
    left.count = 0;

    Bounds right;
    right.min = vec3(FLT_MAX);
    right.max = vec3(-FLT_MAX);
    right.count = 0;

    int start = node.start;
    int end   = node.start + node.count;
    for (int i = start; i < end; i++) {
        Tri& tri = triangles[triIndices[i]];
        if (tri.c[axis] < splitPos) {
            left.min = min(left.min, tri.min);
            left.max = max(left.max, tri.max);
            left.count++;
        } else {
            right.min = min(right.min, tri.min);
            right.max = max(right.max, tri.max);
            right.count++;
        }
    }
    float cost = left.count * area(left.min, left.max) + right.count * area(right.min, right.max);
    return cost;
}

int usedNodes = 0;

void subdivide(int idx, int depth = 0) {
    Node& node = nodes[idx];
    if (node.count <= Config::minVolumeAmount || depth >= Config::maxBVHDepth) return;

    int bestSplit = -1;
    int axis = 0;
    float parentCost = area( node.min, node.max ) * node.count;
    float bestCost = parentCost;
    for (int a = 0; a < 3; ++a) {
        for (int k = node.start; k < node.start + node.count; ++k) {
            float cost = evalSAH( node, a, triangles[triIndices[k]].c[a] );
            if (cost < bestCost) {
                bestCost = cost;
                bestSplit = k;
                axis = a;
            }
        }
    }

    if (bestCost >= parentCost) return;

    float splitPos = triangles[triIndices[bestSplit]].c[axis];

    int i = node.start;
    int j = i + node.count - 1;
    while (i <= j) {
        if (triangles[triIndices[i]].c[axis] < splitPos) i++;
        else swap( triIndices[i], triIndices[j--] );
    }
    int leftCount = i - node.start;
    if (leftCount == 0 || leftCount == node.count) return;

    int leftChildIdx = usedNodes++;
    int rightChildIdx = usedNodes++;

    nodes[leftChildIdx].start = node.start;
    nodes[leftChildIdx].count = leftCount;

    nodes[rightChildIdx].start = i;
    nodes[rightChildIdx].count = node.count - leftCount;
    
    node.start = leftChildIdx;
    node.count = 0;

    shrinkBounds( leftChildIdx );
    shrinkBounds( rightChildIdx );

    subdivide( leftChildIdx, depth + 1 );
    subdivide( rightChildIdx, depth + 1 );
}

void buildBVH(Mesh& mesh) {
    nodes.resize(usedNodes + mesh.triCount * 2 - 1);
    int idx = usedNodes++;
    nodes[idx].start = mesh.triStart;
    nodes[idx].count = mesh.triCount;
    shrinkBounds( idx );
    subdivide( idx );
    mesh.bvhRoot = idx;
    nodes.resize(usedNodes);
}

void buildBVHs(std::vector<Mesh>& meshes) {
    for (Mesh& mesh : meshes) buildBVH(mesh);
}