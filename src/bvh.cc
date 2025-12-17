#include <glm/glm.hpp>
#include <structs.hh>
#include <bvh.hh>

#include <algorithm>
#include <vector>
#include <functional>

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

static float getArea(const TLAS& a, const TLAS& b) {
    vec3 minv = min(vec3(a.min), vec3(b.min));
    vec3 maxv = max(vec3(a.max), vec3(b.max));
    return area(minv, maxv);
}

static bool findBestPairIdx(const std::vector<TLAS>& allEntries, const std::vector<int>& active, int& outActiveA, int& outActiveB) {
    float bestCost = FLT_MAX;
    bool found = false;

    for (int i = 0; i < (int)active.size(); ++i) {
        for (int j = i + 1; j < (int)active.size(); ++j) {
            float cost = getArea(allEntries[active[i]], allEntries[active[j]]);
            if (cost < bestCost) {
                bestCost = cost;
                outActiveA = i;
                outActiveB = j;
                found = true;
            }
        }
    }
    return found;
}

static std::vector<TLAS> getOrdered(const std::vector<TLAS>& allEntries, int rootIdx) {
    std::vector<TLAS> ordered;
    ordered.reserve(allEntries.size());
    std::vector<int> remap(allEntries.size(), -1);

    std::function<int(int)> dfs = [&](int oldIdx) -> int {
        int& newIdx = remap[oldIdx];
        if (newIdx != -1) return newIdx;

        newIdx = (int)ordered.size();
        ordered.push_back(allEntries[oldIdx]);

        TLAS& n = ordered.back();
        if (n.idx == -1) {
            int lNew = dfs(n.left);
            int rNew = dfs(n.right);
            n.left = lNew;
            n.right = rNew;
        } else {
            n.left = 0;
            n.right = 0;
        }

        return newIdx;
    };

    dfs(rootIdx);
    return ordered;
}

void buildTLAS() {
    std::vector<TLAS> allEntries;
    std::vector<int> active;

    allEntries.reserve(meshes.size() + spheres.size());
    active.reserve(meshes.size() + spheres.size());

    for (int i = 0; i < (int)meshes.size(); ++i) {
        if (meshes[0].bvhRoot < 0) continue;

        const Node& root = nodes[meshes[i].bvhRoot];

        TLAS entry;
        entry.min = vec4(root.min, 1.0f);
        entry.max = vec4(root.max, 1.0f);
        entry.idx = i;
        entry.type = 0;
        entry.left = 0;
        entry.right = 0;

        allEntries.push_back(entry);
        active.push_back((int)allEntries.size() - 1);
    }

    for (int si = 0; si < (int)spheres.size(); ++si) {
        const Sph& sph = spheres[si];

        TLAS entry;
        entry.min = vec4(sph.center - vec3(sph.radius), 1.0f);
        entry.max = vec4(sph.center + vec3(sph.radius), 1.0f);
        entry.idx = si;
        entry.type = 1;
        entry.left = 0;
        entry.right = 0;

        allEntries.push_back(entry);
        active.push_back((int)allEntries.size() - 1);
    }

    if (active.empty()) {
        tlas.clear();
        return;
    }

    while (active.size() > 1) {
        int aPos = -1, bPos = -1;
        if (!findBestPairIdx(allEntries, active, aPos, bPos)) break;

        int aIdx = active[aPos];
        int bIdx = active[bPos];

        const TLAS& a = allEntries[aIdx];
        const TLAS& b = allEntries[bIdx];

        TLAS parent;
        parent.min = min(a.min, b.min);
        parent.max = max(a.max, b.max);
        parent.idx = -1;
        parent.type = -1;
        parent.left = aIdx;
        parent.right = bIdx;

        allEntries.push_back(parent);
        int pIdx = (int)allEntries.size() - 1;

        if (aPos > bPos) std::swap(aPos, bPos);
        active.erase(active.begin() + bPos);
        active.erase(active.begin() + aPos);
        active.push_back(pIdx);
    }

    int rootIdx = active[0];

    tlas = getOrdered(allEntries, rootIdx);
}