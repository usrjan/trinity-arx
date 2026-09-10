// TrinityBuildEngine.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include "TrinityFileManager.h"

class TrinityBuildEngine {
    TrinityCore m_core;
    TrinityFileManager m_files;
    AcDbObjectId ensureExists(const std::string& code, const AcGePoint3d& position,
                               const TrinityRotationCompound& rotation, AcDbDatabase* targetDb, int depth = 0);
    AcDbDatabase* buildDwg(const TrinityNeuron& neuron, int depth);
    AcDbDatabase* buildDetail(const TrinityNeuron& detail);
    std::string ensureFileExists(const std::string& code);
public:
    TrinityBuildEngine(const std::string& basePath) : m_files(basePath) {}
    bool init(const char* host, const char* user, const char* pass, const char* db)
        { return m_core.connect(host, user, pass, db); }
    void shutdown() { m_core.disconnect(); }
    int processAllProjects(AcDbDatabase* targetDb);
};