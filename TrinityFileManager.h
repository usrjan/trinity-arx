// TrinityFileManager.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include <string>

// ============================================
// Файловая система кэша DWG:
//   <base>\details\    — детали (лист 1)
//   <base>\assemblies\ — конструкции (листы 2..N)
//   <base>\projects\   — проекты (сборка листов)
// ============================================
class TrinityFileManager {
public:
    explicit TrinityFileManager(const std::string& basePath);

    bool        fileExists(const std::string& code, const std::string& subdir) const;
    std::string getFilePath(const std::string& code, const std::string& subdir) const;

    // Удалить файл <base>\<subdir>\<code>.dwg
    bool removeFile(const std::string& code, const std::string& subdir) const;

    static AcDbDatabase* createEmptyDwg();
    static bool saveDwg(AcDbDatabase* db, const std::string& path);

    // Подключить DWG как XREF и вставить BlockReference в Model Space.
    // Если блок уже есть, но файл удалён — пересоздать блок.
    // Возвращает ObjectId ссылки или kNull.
    static AcDbObjectId attachXref(const std::string& path,
                                   const std::string& name,
                                   const AcGePoint3d& pos,
                                   const TrinityRotationCompound& rot,
                                   AcDbDatabase* targetDb);

    // Каталог по типу нейрона
    static std::string subdirForType(const std::string& type);

    std::string detailsDir()    const { return "details"; }
    std::string assembliesDir() const { return "assemblies"; }
    std::string projectsDir()   const { return "projects"; }

private:
    std::string m_basePath;
};
