#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "TrinityConfig.h"

// Структуры данных, соответствующие таблицам БД
struct ProjectData {
    int id;
    std::string name;
    std::string path;
    std::vector<int> rootNodeIds; // Список корневых узлов проекта
};

struct NodeData {
    int id;
    int projectId;
    std::string name;
    std::string type; // "assembly", "part", etc.
    double x, y, z;   // Позиция
    double rotX, rotY, rotZ; // Вращение
    std::vector<int> childNodeIds; // Дочерние узлы
    std::vector<int> partIds;      // Детали в этом узле
};

struct PartData {
    int id;
    int nodeId;
    std::string name;
    std::string profileType;
    double length;
    double width;
    double height;
    std::string material;
    int layerId;
};

/**
 * @brief Кэш данных для устранения N+1 проблемы.
 * Хранит загруженные из БД проекты, узлы и детали в памяти.
 */
class DataCache {
public:
    static DataCache& Instance();

    // Очистка кэша перед новым запуском
    void Clear();

    // Проекты
    void AddProject(const ProjectData& proj);
    const ProjectData* GetProject(int id) const;

    // Узлы
    void AddNodes(const std::vector<NodeData>& nodes);
    const NodeData* GetNode(int id) const;

    // Детали
    void AddParts(const std::vector<PartData>& parts);
    const PartData* GetPart(int id) const;

    // Статистика
    size_t GetProjectCount() const { return projects_.size(); }
    size_t GetNodeCount() const { return nodes_.size(); }
    size_t GetPartCount() const { return parts_.size(); }

private:
    DataCache() = default;
    ~DataCache() = default;
    DataCache(const DataCache&) = delete;
    DataCache& operator=(const DataCache&) = delete;

    std::unordered_map<int, ProjectData> projects_;
    std::unordered_map<int, NodeData> nodes_;
    std::unordered_map<int, PartData> parts_;
};
