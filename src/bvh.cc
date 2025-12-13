#include <glm/glm.hpp>
#include <structs.hh>
#include <bvh.hh>

#include <algorithm>
#include <vector>


//void buildNode(int idx, vec3 minv, vec3 maxv, std::vector<Type>& idxs) {
//    Node node;
//    node.idx = -1;
//    node.type = -1;
//    node.ownIdx = idx;
//    node.left = -1;
//    node.right = -1;
//    node.min = minv;
//    node.max = maxv;
//    nodes.push_back(node);
//    if (idxs.size() == 1) {
//        Type t = idxs[0];
//        if (t.type == 0) {
//            Tri& tri = triangles[t.idx];
//            nodes[idx].min = min(tri.v0, min(tri.v1, tri.v2));
//            nodes[idx].max = max(tri.v0, max(tri.v1, tri.v2));
//        } else if (t.type == 1) {
//            Sph& sph = spheres[t.idx];
//            nodes[idx].min = sph.center - vec3(sph.radius);
//            nodes[idx].max = sph.center + vec3(sph.radius);
//        }
//        nodes[idx].idx = t.idx;
//        nodes[idx].type = t.type;
//        if (t.type == 0) {
//            int material = triangles[t.idx].materialIdx;
//            if (material >= 0) nodes[idx].materialIdx = material;
//            else nodes[idx].materialIdx = 0;
//        } 
//        if (t.type == 1) {
//            int material = spheres[t.idx].materialIdx;
//            if (material >= 0) nodes[idx].materialIdx = material;
//            else nodes[idx].materialIdx = 0;
//        } 
//    } else {
//        vec3 extent = maxv - minv; 
//        int axis = 0;
//        if (extent.y > extent.x) axis = 1;
//        if (extent.z > extent[axis]) axis = 2;
//        
//        float midPoint = nodes[idx].min[axis] + extent[axis] * 0.5f;
//
//        std::vector<Type> leftIdxs, rightIdxs;
//        for (Type t : idxs) {
//            vec3 center = t.type ? spheres[t.idx].center : triangles[t.idx].c;
//            if (center[axis] < midPoint) {
//                leftIdxs.push_back(t);
//            } else {
//                rightIdxs.push_back(t);
//            }
//        }
//
//        vec3 leftMax = maxv;
//        vec3 leftMin = minv;
//        vec3 rightMax = maxv;
//        vec3 rightMin = minv;
//        leftMax[axis] = midPoint;
//        rightMin[axis] = midPoint;
//
//        int leftIdx = -1;
//        int rightIdx = -1;
//
//        if (leftIdxs.empty() || rightIdxs.empty()) {
//            std::sort(idxs.begin(), idxs.end(), [axis](const Type& a, const Type& b) {
//                vec3 ac = a.type ? spheres[a.idx].center : vec3(triangles[a.idx].c);
//                vec3 bc = b.type ? spheres[b.idx].center : vec3(triangles[b.idx].c);
//                return ac[axis] < bc[axis];
//            });
//            size_t mid = idxs.size() / 2;
//            leftIdxs.assign(idxs.begin(), idxs.begin() + mid);
//            rightIdxs.assign(idxs.begin() + mid, idxs.end());
//        }
//        if (!leftIdxs.empty()) {
//            leftIdx = nodes.size();
//            buildNode(leftIdx, leftMin, leftMax,leftIdxs);
//            nodes[idx].left = leftIdx;
//        }
//        if (!rightIdxs.empty()) {
//            rightIdx = nodes.size();
//            buildNode(rightIdx, rightMin, rightMax, rightIdxs);  
//            nodes[idx].right = rightIdx;
//        }
//
//        if (leftIdx != -1 && rightIdx != -1) {
//            nodes[idx].max = max(nodes[leftIdx].max, nodes[rightIdx].max);
//            nodes[idx].min = min(nodes[leftIdx].min, nodes[rightIdx].min);
//        } else if (leftIdx != -1) {
//            nodes[idx].max = nodes[leftIdx].max;
//            nodes[idx].min = nodes[leftIdx].min;
//        } else if (rightIdx != -1) {
//            nodes[idx].max = nodes[rightIdx].max;
//            nodes[idx].min = nodes[rightIdx].min;
//        }
//    }
//}

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

void subdivide(int idx, int depth = 0) {
    Node& node = nodes[idx];
    if (node.count <= Config::minVolumeAmount || depth >= Config::maxBVHDepth) return;
    
    vec3 extent = node.max - node.min;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    float splitPos = node.min[axis] + extent[axis] * 0.5f;

    float cmin = FLT_MAX, cmax = -FLT_MAX;
    for (int k = node.start; k < node.start + node.count; ++k) {
        const vec3 c = triangles[triIndices[k]].c;
        cmin = std::min(cmin, c[axis]);
        cmax = std::max(cmax, c[axis]);
    }
    splitPos = 0.5f * (cmin + cmax);


    int i = node.start;
    int j = i + node.count - 1;
    while (i <= j) {
        if (triangles[triIndices[i]].c[axis] < splitPos) i++;
        else swap( triIndices[i], triIndices[j--] );
    }
    int leftCount = i - node.start;
    if (leftCount == 0 || leftCount == node.count) return;

    Node leftNode;
    leftNode.start = node.start;
    leftNode.count = leftCount;
    leftNode.mat = 0;
    leftNode.left = -1;
    leftNode.right = -1;
    nodes.push_back(leftNode);
    int leftChildIdx = static_cast<int>(nodes.size() - 1);
    shrinkBounds( leftChildIdx );
    
    Node rightNode;
    rightNode.start = i;
    rightNode.count = node.count - leftCount;
    rightNode.mat = 0;
    rightNode.left = -1;
    rightNode.right = -1;
    nodes.push_back(rightNode);
    int rightChildIdx = static_cast<int>(nodes.size() - 1);
    shrinkBounds( rightChildIdx );

    nodes[idx].left = leftChildIdx;
    nodes[idx].right = rightChildIdx;

    subdivide( leftChildIdx, depth + 1 );
    subdivide( rightChildIdx, depth + 1 );
}

void buildBVH() {
    Node root;
    root.start = 0;
    root.count = static_cast<int>(triIndices.size());
    root.min = vec3(FLT_MAX);
    root.max = vec3(-FLT_MAX);
    root.left = -1;
    root.right = -1;
    nodes.push_back(root);
    int idx = static_cast<int>(nodes.size() - 1);
    shrinkBounds( idx );
    subdivide( idx );
}

//void tightenBounds(int index) {
//    Node* node = &nodes[index];
//    if (node->idx != -1) {
//        if (node->type == 0) {
//            Tri& tri = triangles[node->idx];
//            node->min = min(tri.v0, min(tri.v1, tri.v2));
//            node->max = max(tri.v0, max(tri.v1, tri.v2));
//        } else if (node->type == 1) {
//            Sph& sph = spheres[node->idx];
//            node->min = sph.center - vec3(sph.radius);
//            node->max = sph.center + vec3(sph.radius);
//        }
//    } else {
//        if (node->left != -1) tightenBounds(node->left);
//        if (node->right != -1) tightenBounds(node->right);
//        
//        if (node->left != -1 && node->right != -1) {
//            node->max = max(nodes[node->left].max, nodes[node->right].max);
//            node->min = min(nodes[node->left].min, nodes[node->right].min);
//        } else if (node->left != -1) {
//            node->max = nodes[node->left].max;
//            node->min = nodes[node->left].min;
//        } else if (node->right != -1) {
//            node->max = nodes[node->right].max;
//            node->min = nodes[node->right].min;
//        }
//    }
//}