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

    // Санитизация имени файла: код нейрона/детали приходит из БД (UTF-8) и
    // используется как имя DWG без изменений. Функция заменяет запрещённые
    // символы Windows (< > : " / \ | ? *), управляющие символы <0x20, точки
    // в начале/конце (блокируют '..' path traversal и имена с точкой на конце)
    // на '_'. Пустой результат -> "_empty". Возвращает исходную строку без
    // изменений, если она уже корректна.
    static std::string sanitizeFileName(const std::string& nameUtf8);

    bool fileExists(const std::string& code, const std::string& subdir) const;
    std::string getFilePath(const std::string& code, const std::string& subdir) const;

    // Единый хелпер: подкаталог по типу нейрона (было 3 копии if/else в
    // TrinityBuildEngine.cpp). detail -> details, assembly/construction ->
    // assemblies, всё остальное -> projects.
    std::string subdirForType(const std::string& type) const;
    // Подкаталог + полный путь к DWG за один вызов (устраняет связку из
    // трёх строк «subdir = ...; filePath = getFilePath(...)» в каждом месте).
    std::string getFilePathForNeuron(const std::string& code, const std::string& type) const;

    static AcDbDatabase* createEmptyDwg();
    static bool saveDwg(AcDbDatabase* db, const std::string& path);

    static AcDbObjectId attachXref(
        const std::string& path,
        const std::string& name,
        const AcGePoint3d& pos,
        const TrinityRotationCompound& rot,
        AcDbDatabase* targetDb
    );

    // «Лёгкая» проверка: является ли уже существующий блок в целевой базе
    // XREF-ссылкой ровно на данный файл (тот же путь после нормализации).
    // Нужна для логики «не пересобирать то, что уже есть»: если деталь или
    // конструкция уже представлена в собираемой базе корректной ссылкой,
    // её не рисуют заново — существующая запись переиспользуется.
    // Блок должен существовать в pTargetDb (проверяется has()).
    static bool blockMatchesXrefPath(AcDbDatabase* pTargetDb,
                                     const std::wstring& blockNameW,
                                     const std::string& pathUtf8);

    // Сравнение двух путей как канонических Windows-путей без учёта
    // регистра и стиля разделителей ('C:\a\B.dwg' == 'c:/a/b.dwg').
    // Wide-перегрузка — основная; string-версия принимает UTF-8 пути
    // (формат нашего конфига/БД) и конвертирует сама.
    static bool sameNormalizedPath(const std::wstring& a, const std::wstring& b);
    static bool sameNormalizedPath(const std::string& aUtf8, const std::string& bUtf8);

    std::string detailsDir() const { return "details"; }
    std::string assembliesDir() const { return "assemblies"; }
    std::string projectsDir() const { return "projects"; }
};