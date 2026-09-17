#include "DataCache.h"

// Singleton implementation
DataCache& DataCache::Instance() {
    static DataCache instance;
    return instance;
}

void DataCache::Clear() {
    projects_.clear();
    nodes_.clear();
    parts_.clear();
}

// Projects
void DataCache::AddProject(const ProjectData& proj) {
    projects_[proj.id] = proj;
}

const ProjectData* DataCache::GetProject(int id) const {
    auto it = projects_.find(id);
    return (it != projects_.end()) ? &it->second : nullptr;
}

// Nodes
void DataCache::AddNodes(const std::vector<NodeData>& nodes) {
    for (const auto& node : nodes) {
        nodes_[node.id] = node;
    }
}

const NodeData* DataCache::GetNode(int id) const {
    auto it = nodes_.find(id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

// Parts
void DataCache::AddParts(const std::vector<PartData>& parts) {
    for (const auto& part : parts) {
        parts_[part.id] = part;
    }
}

const PartData* DataCache::GetPart(int id) const {
    auto it = parts_.find(id);
    return (it != parts_.end()) ? &it->second : nullptr;
}
