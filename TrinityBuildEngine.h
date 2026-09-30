// TrinityBuildEngine.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include "TrinityFileManager.h"

class TrinityBuildEngine {
private:
    TrinityCore m_core;
    TrinityFileManager m_files;

    // Примечание: вставка готового DWG проекта в текущий чертёж не
    // выполняется (ensureExists удалён как мёртвый код, реализованная
    // вместо него insertProjectToTarget вызывала ошибку 320 при работе
    // с активным документом и убрана). Результат сборки — файлы DWG
    // на диске; attachXref используется только внутри buildDwg для
    // вставки детей во временную базу.

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

    // Гарантирует существование родительской директории файла (UTF-8 путь).
    // Нужен перед saveDwg: промежуточные папки могут отсутствовать,
    // а путь может содержать кириллицу (создаётся через _wmkdir).
    bool ensureDirectoryForFile(const std::string& filePathUtf8) {
        return TrinityFileManager::createDirectoryRecursiveA(
                   TrinityFileManager::parentDir(filePathUtf8));
    }

public:
    TrinityBuildEngine(const std::string& basePath) : m_files(basePath) {}

    bool init(const char* host, const char* user, const char* pass, const char* db) {
        return m_core.connect(host, user, pass, db);
    }

    void shutdown() { m_core.disconnect(); }

    // Главный метод: обработать все pending-проекты
    int processAllProjects(AcDbDatabase* targetDb);
};