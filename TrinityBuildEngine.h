// TrinityBuildEngine.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"
#include "TrinityFileManager.h"

class TrinityBuildEngine {
private:
    TrinityCore m_core;
    TrinityFileManager m_files;

    // Рекурсивное обеспечение существования DWG + вставка XREF
    // Файл берётся у ensureFileExists (там же вся логика постройки),
    // здесь только attachXref в целевую базу.
    // При position == nullPos вставку не выполняет (возвращает kNull) —
    // этот режим используется рекурсией, когда нужен только файл.
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

    // Общие правила путей: тип нейрона → подкаталог → полный путь файла
    // (используется в ensureFileExists и deleteProjectFiles)
    std::string subdirForType(const std::string& type);
    std::string filePathForNeuron(const TrinityNeuron& neuron);

    // Обеспечить существование файла детали/конструкции
    // Загружает нейрон, при отсутствии файла строит его (buildDetail/buildDwg)
    // и сохраняет. Вставки XREF НЕ делает. Возвращает путь к файлу
    // или пустую строку при ошибке.
    std::string ensureFileExists(const std::string& code, int depth = 0);

    // Рекурсивное удаление файлов проекта и всех его детей
    void deleteProjectFiles(const std::string& code);

public:
    TrinityBuildEngine(const std::string& basePath) : m_files(basePath) {}

    bool init(const char* host, const char* user, const char* pass, const char* db) {
        return m_core.connect(host, user, pass, db);
    }

    void shutdown() { m_core.disconnect(); }

    // Главный метод: обработать все pending-проекты
    int processAllProjects(AcDbDatabase* targetDb);
};