// TrinityCore.h
#pragma once
#include "StdAfx.h"

struct TrinityPosition { double x=0,y=0,z=0; AcGePoint3d toAcGe() const { return AcGePoint3d(x,y,z); } };
struct TrinityRotation { double x=0,y=0,z=0,angle=0; };
struct TrinityRotationCompound { TrinityRotation rotations[2]; int count=0; };

struct TrinityNeuron {
    int id=0, width=0, height=0, thickness=10, processCode=0;
    std::string code, type, category, material, status, jsonData;
};

struct TrinitySynapse {
    int id=0, parentId=0, childId=0;
    std::string childCode, status;
    TrinityPosition position;
    TrinityRotationCompound rotation;
};

class TrinityCore {
    MYSQL* m_mysql = nullptr;
    bool m_connected = false;
public:
    TrinityCore() = default;
    ~TrinityCore();
    bool connect(const char* host, const char* user, const char* pass, const char* db);
    void disconnect();
    bool isConnected() const { return m_connected; }
    MYSQL* handle() { return m_mysql; }

    TrinityNeuron* loadNeuronByCode(const std::string& code);
    TrinityNeuron* loadNeuronById(int id);
    std::vector<TrinitySynapse> loadChildren(int parentId);
    std::vector<TrinityNeuron> loadPendingProjects();
    bool markNeuronDone(int id);

    static TrinityNeuron parseNeuronRow(MYSQL_ROW row);
    static TrinitySynapse parseSynapseRow(MYSQL_ROW row);
    static TrinityPosition parsePosition(const std::string& json);
    static TrinityRotationCompound parseRotation(const std::string& json);
};