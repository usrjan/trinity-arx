// TrinityFileManager.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include <string>

class TrinityFileManager {
    std::string m_basePath;

public:
    TrinityFileManager(const std::string& basePath);

    bool fileExists(const std::string& code, const std::string& subdir) const;
    std::string getFilePath(const std::string& code, const std::string& subdir) const;

    // Удаляет DWG-файл и связанные с ним lock-файлы AutoCAD (*.dwl, *.dwl2).
    // Возвращает true, если удалён хотя бы основной файл (или его не было).
    bool deleteDwgWithLocks(const std::string& code, const std::string& subdir) const;

    static AcDbDatabase* createEmptyDwg();
    static bool saveDwg(AcDbDatabase* db, const std::string& path);

    static AcDbObjectId attachXref(
        const std::string& path,
        const std::string& name,
        const AcGePoint3d& pos,
        const TrinityRotationCompound& rot,
        AcDbDatabase* targetDb
    );

    std::string detailsDir() const { return "details"; }
    std::string assembliesDir() const { return "assemblies"; }
    std::string projectsDir() const { return "projects"; }
};