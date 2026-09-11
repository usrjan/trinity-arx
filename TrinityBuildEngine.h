// TrinityBuildEngine.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include "TrinityFileManager.h"

class TrinityBuildEngine {
private:
    TrinityCore m_core;
    TrinityFileManager m_files;

    // Рекурсивное обеспечение существования DWG
    // Если файл есть — вставляет XREF и возвращает его ID
    // Если файла нет — строит его и потом вставляет XREF
    AcDbObjectId ensureExists(const std::string& code,
                               const AcGePoint3d& position,
                               const TrinityRotationCompound& rotation,
                               AcDbDatabase* targetDb,
                               int depth = 0);

    // Построить DWG конструкции/проекта из детей
    // Дети вставляются как XREF во временную базу,
    // потом wblock в чистую базу
    AcDbDatabase* buildDwg(const TrinityNeuron& neuron, int depth);

    // Построить DWG детали (конечный уровень)
    // Геометрия → временная база → wblock → чистая база
    AcDbDatabase* buildDetail(const TrinityNeuron& detail);

    // Обеспечить существование файла детали/конструкции
    // Без вставки XREF. Возвращает путь к файлу.
    // Используется в buildDwg для рекурсивной подготовки детей.
    std::string ensureFileExists(const std::string& code, int depth = 0);

public:
    TrinityBuildEngine(const std::string& basePath) : m_files(basePath) {}

    bool init(const char* host, const char* user, const char* pass, const char* db) {
        return m_core.connect(host, user, pass, db);
    }

    void shutdown() { m_core.disconnect(); }

    // Главный метод: обработать все pending-проекты
    int processAllProjects(AcDbDatabase* targetDb);
};