// TrinityBuildEngine.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include "TrinityFileManager.h"

// ============================================
// Рекурсивный движок сборки DWG-файлов:
//   project → construction → detail.
// Готовые файлы кэшируются в подкаталогах базы;
// недостающие строятся на лету и сохраняются.
// Движок владеет подключением к БД (init/shutdown).
// ============================================
class TrinityBuildEngine {
public:
    explicit TrinityBuildEngine(const std::string& basePath)
        : m_files(basePath) {}

    bool init(const char* host, const char* user,
              const char* pass, const char* db) {
        return m_core.connect(host, user, pass, db);
    }
    void shutdown() { m_core.disconnect(); }

    // Главный метод: обработать все pending-проекты.
    int processAllProjects(AcDbDatabase* targetDb);

    static constexpr int MAX_DEPTH = 20;

private:
    // Рекурсивное обеспечение существования DWG:
    // файл есть — вставляет XREF; нет — строит и потом вставляет.
    AcDbObjectId ensureExists(const std::string& code,
                              const AcGePoint3d& position,
                              const TrinityRotationCompound& rotation,
                              AcDbDatabase* targetDb,
                              int depth = 0);

    // Построить DWG конструкции/проекта из детей
    // (дети — XREF во временную базу, затем wblock в чистую)
    AcDbDatabase* buildDwg(const TrinityNeuron& neuron, int depth);

    // Построить DWG детали (конечный уровень):
    // геометрия → временная база → wblock → чистая база
    AcDbDatabase* buildDetail(const TrinityNeuron& detail);

    // Обеспечить существование файла без вставки XREF.
    // Возвращает путь ('' при ошибке). Используется в buildDwg.
    std::string ensureFileExists(const std::string& code, int depth = 0);

    // Рекурсивное удаление файлов проекта и всех его детей
    void deleteProjectFiles(const std::string& code);

    TrinityCore        m_core;
    TrinityFileManager m_files;
};
