// TrinityBuildEngine.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include "TrinityFileManager.h"

class TrinityBuildEngine {
private:
    TrinityCore m_core;
    TrinityFileManager m_files;

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

    // Рекурсивное удаление файлов проекта и всех его детей
    void deleteProjectFiles(const std::string& code);

public:
    TrinityBuildEngine(const std::string& basePath) : m_files(basePath) {}

    bool init(const char* host, const char* user, const char* pass, const char* db) {
        return m_core.connect(host, user, pass, db);
    }

    void shutdown() { m_core.disconnect(); }

    // Доступ к ядру (запросы к БД из команд разработки)
    TrinityCore& core() { return m_core; }

    // Главный метод: обработать все pending-проекты
    int processAllProjects(AcDbDatabase* targetDb);

    // Отрисовать все детали категории прямо в целевую базу (текущий чертеж).
    // Каждая деталь — AcDbBlockTableRecord (блок TRIB_<code>) с геометрией
    // и XDATA (категория, размеры, толщина), + AcDbBlockReference со
    // стандартными параметрическими свойствами (Height/Width/Rotation).
    // Возвращает количество вставленных блоков.
    int drawDetailsInDrawing(AcDbDatabase* targetDb, const std::string& category);
};