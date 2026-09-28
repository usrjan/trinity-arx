// TrinityFileManager.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include <string>

class TrinityFileManager {
    std::string m_basePath;

public:
    TrinityFileManager(const std::string& basePath);

    // Рекурсивное создание директории (все промежуточные папки).
    // Unicode-версия (_wmkdir): корректно работает с кириллицей и прочими
    // не-ASCII путями. ANSI-строки трактуются как UTF-8.
    static bool createDirectoryRecursiveA(const std::string& pathUtf8);
    static bool createDirectoryRecursiveW(const wchar_t* wpath);

    // Родительская директория полного пути к файлу ('\' и '/' — разделители).
    static std::string parentDir(const std::string& filePath);

    bool fileExists(const std::string& code, const std::string& subdir) const;
    std::string getFilePath(const std::string& code, const std::string& subdir) const;

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