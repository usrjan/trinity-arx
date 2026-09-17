// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"

// ============================================================
// ГЛАВНЫЙ РЕКУРСИВНЫЙ МЕТОД
// ============================================================
// Логика:
//   1. Загружаем нейрон по коду
//   2. Определяем подкаталог (details/assemblies/projects)
//   3. Если файл есть — вставляем XREF и выходим
//   4. Если файла нет:
//      - для detail  → buildDetail
//      - для assembly/construction/project → buildDwg
//   5. Сохраняем DWG через saveDwg
//   6. Вставляем XREF
// ============================================================
AcDbObjectId TrinityBuildEngine::ensureExists(const std::string& code,
                                                const AcGePoint3d& position,
                                                const TrinityRotationCompound& rotation,
                                                AcDbDatabase* targetDb,
                                                int depth) {
    // Защита от бесконечной рекурсии
    if (depth > 20) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] MAX DEPTH reached for %s\n"), wCode);
        free(wCode);
        return AcDbObjectId::kNull;
    }

    // 1. Загружаем нейрон с RAII-обёрткой
    TrinityNeuronPtr pNeuron(m_core.loadNeuronByCode(code));
    if (!pNeuron) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] Neuron not found: %s\n"), wCode);
        free(wCode);
        return AcDbObjectId::kNull;
    }

    TrinityNeuron neuron = *pNeuron;
    // delete pNeuron больше не нужен - удалится автоматически при выходе из функции

    // 2. Определяем подкаталог
    std::string subdir;
    if (neuron.type == "detail") {
        subdir = m_files.detailsDir();
    } else if (neuron.type == "assembly" || neuron.type == "construction") {
        subdir = m_files.assembliesDir();
    } else {
        subdir = m_files.projectsDir();
    }

    std::string filePath = m_files.getFilePath(neuron.code, subdir);

    // 3. Если файл существует — вставляем XREF
    if (m_files.fileExists(neuron.code, subdir)) {
        wchar_t* wCode = utf2uni(neuron.code.c_str());
        acutPrintf(_T("\n[BuildEngine] EXISTS: %s (depth=%d)\n"), wCode, depth);
        free(wCode);

        return m_files.attachXref(filePath, neuron.code, position, rotation, targetDb);
    }

    // 4. Файла нет — строим
    wchar_t* wCode = utf2uni(neuron.code.c_str());
    wchar_t* wType = utf2uni(neuron.type.c_str());
    acutPrintf(_T("\n[BuildEngine] BUILDING: %s (type=%s, depth=%d)\n"),
               wCode, wType, depth);
    free(wCode);
    free(wType);

    DatabasePtr cleanDb(nullptr);

    if (neuron.type == "detail") {
        cleanDb.reset(buildDetail(neuron));
    } else {
        cleanDb.reset(buildDwg(neuron, depth));
    }

    if (!cleanDb) {
        return AcDbObjectId::kNull;
    }

    // 5. Сохраняем
    m_files.saveDwg(cleanDb.get(), filePath);
    // delete cleanDb больше не нужен - удалится автоматически

    // Пауза для файловой системы
    Sleep(200);

    // 6. Вставляем XREF
    return m_files.attachXref(filePath, neuron.code, position, rotation, targetDb);
}

// ============================================================
// ПОСТРОЕНИЕ ДЕТАЛИ
// ============================================================
// Порядок:
//   1. tempDb — временная база
//   2. Слой материала
//   3. build() → солид (щит/планка/стенка)
//   4. appendAcDbEntity(solidId, solid) — БЕЗ close()
//   5. Если rib → болты (drawBoltMarkers)
//   6. Если sidewall → сверление (booleanOper + erase)
//   7. solid->close() — ПОСЛЕ всех операций
//   8. Атрибуты (DETAIL_CODE на слое _tag)
//   9. wblock → cleanDb
//  10. Переназначение слоёв: solid → материал, circle → _bolt, attr → _tag
// ============================================================
AcDbDatabase* TrinityBuildEngine::buildDetail(const TrinityNeuron& detail) {
    DatabasePtr tempDb(new AcDbDatabase(Adesk::kTrue, Adesk::kTrue));
    if (!tempDb) return nullptr;

    // Слой материала
    TrinityLayerManager::createOrGetLayer(tempDb.get(), detail.material);

    // Строим геометрию
    AcDb3dSolid* solid = TrinityGeometryBuilder::build(detail);
    if (!solid) {
        return nullptr; // tempDb удалится автоматически
    }

    // Назначаем слой
    std::string layer = TrinityLayerManager::layerName(detail.material);
    wchar_t layerW[256];
    MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, layerW, 256);
    solid->setLayer(layerW);

    // Получаем Model Space
    AcDbBlockTable* pBt = nullptr;
    tempDb->getSymbolTable(pBt, AcDb::kForRead);
    AcDbBlockTableRecord* pMs = nullptr;
    pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBt->close();

    AcDbObjectIdArray ids;

    // Добавляем солид (но НЕ закрываем — нужен для booleanOper)
    AcDbObjectId solidId;
    pMs->appendAcDbEntity(solidId, solid);

    // ============================================================
    // БОЛТЫ ДЛЯ ПЛАНОК
    // ============================================================
    if (detail.category == "rib") {
        TrinityLayerManager::ensureBoltLayer(tempDb.get());
        TrinityGeometryBuilder::drawBoltMarkers(detail, pMs, ids);
    }

    // ============================================================
    // СВЕРЛЕНИЕ ОТВЕРСТИЙ ДЛЯ БОКОВЫХ СТЕНОК
    // ============================================================
    if (detail.category == "sidewall") {
        double holeRadius = 4.0;                // Ø8 мм
        double height = detail.thickness + 2.0; // чуть больше толщины

        std::vector<AcGePoint3d> holePositions;

        // Парсим holes из JSON
        size_t holesPos = detail.jsonData.find("\"holes\"");
        if (holesPos != std::string::npos) {
            size_t arrStart = detail.jsonData.find('[', holesPos);
            size_t arrEnd = detail.jsonData.find(']', arrStart);

            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string holesStr = detail.jsonData.substr(arrStart, arrEnd - arrStart + 1);

                size_t objPos = 0;
                while ((objPos = holesStr.find("\"x\"", objPos)) != std::string::npos) {
                    double x = 0, y = 0;

                    size_t xVal = holesStr.find(':', objPos);
                    if (xVal != std::string::npos) {
                        x = atof(holesStr.c_str() + xVal + 1);
                    }

                    size_t yPos = holesStr.find("\"y\"", objPos);
                    if (yPos != std::string::npos) {
                        size_t yVal = holesStr.find(':', yPos);
                        if (yVal != std::string::npos) {
                            y = atof(holesStr.c_str() + yVal + 1);
                        }
                    }

                    holePositions.push_back(AcGePoint3d(x, y, 0));
                    objPos = yPos + 1;
                }
            }
        }

        // Сверлим
        for (const auto& pos : holePositions) {
            SolidPtr pCylinder(new AcDb3dSolid());
            pCylinder->createFrustum(height, holeRadius, holeRadius, holeRadius);

            AcGeMatrix3d mat;
            mat.setToIdentity();
            mat.setTranslation(AcGeVector3d(pos.x, pos.y, detail.thickness / 2.0));
            pCylinder->transformBy(mat);

            Acad::ErrorStatus es = solid->booleanOper(AcDb::kBoolSubtract, pCylinder.get());

            // После booleanOper цилиндр либо NULL solid, либо невалиден
            // Освобождаем память через release() - удалим в деструкторе
            pCylinder.release();

            if (es == Acad::eOk) {
                acutPrintf(_T("\n[Geometry] Hole drilled at (%.1f, %.1f)\n"), pos.x, pos.y);
            }
            else {
                acutPrintf(_T("\n[Geometry] booleanOper failed at (%.1f, %.1f): %d\n"),
                    pos.x, pos.y, es);
            }
        }
    }

    // ============================================================
    // ЗАКРЫВАЕМ СОЛИД ПОСЛЕ ВСЕХ ОПЕРАЦИЙ
    // ============================================================
    solid->close();
    ids.append(solidId);

    // ============================================================
    // АТРИБУТЫ (DETAIL_CODE на слое _tag)
    // ============================================================
    TrinityAttributeBuilder::ensureTagLayer(tempDb.get());
    AcDbObjectId attrId = TrinityAttributeBuilder::addDetailCode(pMs, detail.code);
    if (attrId != AcDbObjectId::kNull) {
        ids.append(attrId);
    }

    pMs->close();

    // ============================================================
    // WBLOCK → чистая база
    // ============================================================
    AcDbDatabase* cleanDb = nullptr;
    Acad::ErrorStatus es = tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    // tempDb удалится автоматически

    if (es != Acad::eOk || !cleanDb) {
        delete cleanDb;
        return nullptr;
    }

    // ============================================================
    // СОЗДАЁМ СЛОИ В ЧИСТОЙ БАЗЕ
    // ============================================================
    TrinityLayerManager::createOrGetLayer(cleanDb, detail.material);
    TrinityAttributeBuilder::ensureTagLayer(cleanDb);

    // Если это планка — создаём и слой _bolt
    if (detail.category == "rib") {
        TrinityLayerManager::ensureBoltLayer(cleanDb);
    }

    // ============================================================
    // ПЕРЕНАЗНАЧАЕМ СЛОИ ОБЪЕКТАМ
    // ============================================================
    AcDbBlockTable* pBt2 = nullptr;
    cleanDb->getSymbolTable(pBt2, AcDb::kForRead);
    AcDbBlockTableRecord* pMs2 = nullptr;
    pBt2->getAt(ACDB_MODEL_SPACE, pMs2, AcDb::kForWrite);
    pBt2->close();

    AcDbBlockTableRecordIterator* pIter = nullptr;
    pMs2->newIterator(pIter);

    if (pIter) {
        wchar_t matLayerW[256];
        MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, matLayerW, 256);

        for (pIter->start(); !pIter->done(); pIter->step()) {
            AcDbEntity* pEnt = nullptr;
            if (pIter->getEntity(pEnt, AcDb::kForWrite) == Acad::eOk && pEnt) {
                if (pEnt->isKindOf(AcDb3dSolid::desc())) {
                    pEnt->setLayer(matLayerW);        // солид → материал
                } else if (pEnt->isKindOf(AcDbCircle::desc())) {
                    pEnt->setLayer(_T("_bolt"));       // кружочек → _bolt
                } else {
                    pEnt->setLayer(_T("_tag"));        // атрибут → _tag
                }
                pEnt->close();
            }
        }
        delete pIter;
    }

    pMs2->close();

    // Финальный отчёт
    wchar_t* wCode = utf2uni(detail.code.c_str());
    acutPrintf(_T("\n[BuildEngine] Detail built: %s\n"), wCode);
    free(wCode);

    return cleanDb;
}

// ============================================================
// ПОСТРОЕНИЕ КОНСТРУКЦИИ/ПРОЕКТА
// ============================================================
// Логика:
//   1. tempDb — временная база
//   2. Для каждого ребёнка:
//      - ensureFileExists — убедиться, что файл есть (без XREF)
//      - attachXref — вставить XREF ребёнка во ВРЕМЕННУЮ базу
//      - собрать ObjectId в массив
//   3. wblock → cleanDb
// ============================================================
AcDbDatabase* TrinityBuildEngine::buildDwg(const TrinityNeuron& neuron, int depth) {
    if (neuron.type == "detail") {
        return buildDetail(neuron);
    }

    DatabasePtr tempDb(new AcDbDatabase(Adesk::kTrue, Adesk::kTrue));
    if (!tempDb) return nullptr;

    AcDbObjectIdArray ids;

    auto children = m_core.loadChildren(neuron.id);

    wchar_t* wCode = utf2uni(neuron.code.c_str());
    acutPrintf(_T("\n[BuildEngine] Building %s: %d children (depth=%d)\n"),
        wCode, static_cast<int>(children.size()), depth);
    free(wCode);

    for (auto& syn : children) {
        AcGePoint3d childPos = syn.position.toAcGe();

        wchar_t* wChild = utf2uni(syn.childCode.c_str());
        acutPrintf(_T("\n[BuildEngine] Child: %s (depth=%d)\n"), wChild, depth);
        free(wChild);

        std::string childFilePath = ensureFileExists(syn.childCode, depth + 1);
        if (childFilePath.empty()) {
            acutPrintf(_T("\n[BuildEngine] ensureFileExists returned EMPTY\n"));
            continue;
        }

        acutPrintf(_T("\n[BuildEngine] Got file path, attaching XREF...\n"));

        // НЕ держим Model Space открытым — attachXref сам его откроет
        AcDbObjectId childId = m_files.attachXref(
            childFilePath, syn.childCode, childPos, syn.rotation, tempDb.get());

        if (childId != AcDbObjectId::kNull) {
            ids.append(childId);
            acutPrintf(_T("\n[BuildEngine] XREF appended to ids\n"));
        }
        else {
            acutPrintf(_T("\n[BuildEngine] attachXref returned kNull\n"));
        }
    }

    // Получаем Model Space ТОЛЬКО ПОСЛЕ цикла, для закрытия
    // Или вообще не открываем его — wblock сам разберётся
    // (мы не добавляли ничего вручную в Model Space, только через attachXref)

    acutPrintf(_T("\n[BuildEngine] buildDwg loop done, ids.length=%d\n"), (int)ids.length());

    if (ids.isEmpty()) {
        return nullptr; // tempDb удалится автоматически
    }

    AcDbDatabase* cleanDb = nullptr;
    Acad::ErrorStatus es = tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    // tempDb удалится автоматически

    if (es != Acad::eOk || !cleanDb) {
        delete cleanDb;
        return nullptr;
    }

    return cleanDb;
}

// ============================================================
// ОБЕСПЕЧИТЬ СУЩЕСТВОВАНИЕ ФАЙЛА (без вставки XREF)
// ============================================================
// Используется внутри buildDwg для рекурсивной подготовки детей.
// Возвращает путь к файлу или пустую строку при ошибке.
// ============================================================
std::string TrinityBuildEngine::ensureFileExists(const std::string& code, int depth) {

    wchar_t* wCode = utf2uni(code.c_str());
    acutPrintf(_T("\n[ensureFileExists] START for %s (depth=%d)\n"), wCode, depth);
    free(wCode);


    // Защита от бесконечной рекурсии
    if (depth > 20) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] MAX DEPTH for %s\n"), wCode);
        free(wCode);
        return "";
    }

    // Загружаем нейрон с RAII-обёрткой
    TrinityNeuronPtr pNeuron(m_core.loadNeuronByCode(code));
    if (!pNeuron) return "";

    TrinityNeuron neuron = *pNeuron;
    // delete pNeuron больше не нужен - удалится автоматически

    // Определяем подкаталог
    std::string subdir;
    if (neuron.type == "detail") {
        subdir = m_files.detailsDir();
    } else if (neuron.type == "assembly" || neuron.type == "construction") {
        subdir = m_files.assembliesDir();
    } else {
        subdir = m_files.projectsDir();
    }

    std::string filePath = m_files.getFilePath(neuron.code, subdir);

    // Если файл уже есть — возвращаем путь
    if (m_files.fileExists(neuron.code, subdir)) {
        acutPrintf(_T("\n[ensureFileExists] EXISTS\n"));
        return filePath;
    }

    acutPrintf(_T("\n[ensureFileExists] File not exists, building...\n"));

    // Строим
    AcDbDatabase* db = nullptr;
    if (neuron.type == "detail") {
        db = buildDetail(neuron);
    }
    else {
        db = buildDwg(neuron, depth);
    }

    if (!db) {
        acutPrintf(_T("\n[ensureFileExists] build failed, db is null\n"));
        return "";
    }

    // Сохраняем
    acutPrintf(_T("\n[ensureFileExists] db built, saving...\n"));
    m_files.saveDwg(db, filePath);
    delete db;
    Sleep(200);

    acutPrintf(_T("\n[ensureFileExists] Done\n"));
    return filePath;
}

// ============================================================
// ОБРАБОТКА ВСЕХ PENDING-ПРОЕКТОВ
// ============================================================
int TrinityBuildEngine::processAllProjects(AcDbDatabase* targetDb) {
    auto projects = m_core.loadPendingProjects();
    if (projects.empty()) return 0;

    acutPrintf(_T("\n[BuildEngine] Processing %d projects...\n"),
        static_cast<int>(projects.size()));

    // Предварительная загрузка данных в кэш для устранения N+1 проблемы
    for (auto& proj : projects) {
        initializeCache(proj.id);
    }

    for (auto& proj : projects) {
        // Создаём файл проекта (рекурсивно)
        std::string actualPath = ensureFileExists(proj.code, 0);

        if (actualPath.empty()) {
            wchar_t* wCode = utf2uni(proj.code.c_str());
            acutPrintf(_T("\n[BuildEngine] Failed to create project: %s\n"), wCode);
            free(wCode);
            continue;
        }

        // Отмечаем done — файл создан
        m_core.markNeuronDone(proj.id);

        wchar_t* wCode = utf2uni(proj.code.c_str());
        acutPrintf(_T("\n[BuildEngine] Project done: %s\n"), wCode);
        free(wCode);
    }

    return static_cast<int>(projects.size());
}

// ============================================================
// ИНИЦИАЛИЗАЦИЯ КЭША ДЛЯ ПРОЕКТА
// ============================================================
void TrinityBuildEngine::initializeCache(int projectId) {
    acutPrintf(_T("\n[BuildEngine] Initializing cache for project %d...\n"), projectId);

    // Загружаем все узлы проекта одним запросом
    auto nodes = m_core.loadAllNodesForProject(projectId);
    
    std::vector<NodeData> nodeDataList;
    for (const auto& neuron : nodes) {
        NodeData nd;
        nd.id = neuron.id;
        nd.projectId = projectId;
        nd.name = neuron.code;
        nd.type = neuron.type;
        nd.x = nd.y = nd.z = 0; // Позиция будет получена из синапсов
        nd.rotX = nd.rotY = nd.rotZ = 0;
        
        // Загружаем детей для этого узла
        auto children = m_core.loadChildren(neuron.id);
        for (const auto& child : children) {
            nd.childNodeIds.push_back(child.childId);
        }
        
        // Если это деталь, добавляем partIds
        if (neuron.type == "detail") {
            nd.partIds.push_back(neuron.id);
        }
        
        nodeDataList.push_back(nd);
    }
    
    DataCache::Instance().AddNodes(nodeDataList);
    
    // Загружаем все детали проекта
    auto details = m_core.loadAllDetails();
    
    std::vector<PartData> partDataList;
    for (const auto& detail : details) {
        PartData pd;
        pd.id = detail.id;
        pd.nodeId = detail.id;
        pd.name = detail.code;
        pd.profileType = detail.category;
        pd.length = detail.width;
        pd.width = detail.height;
        pd.height = detail.thickness;
        pd.material = detail.material;
        pd.layerId = 0; // Будет определён позже
        
        partDataList.push_back(pd);
    }
    
    DataCache::Instance().AddParts(partDataList);
    
    acutPrintf(_T("\n[BuildEngine] Cache initialized: %d nodes, %d parts\n"), 
               static_cast<int>(nodeDataList.size()), 
               static_cast<int>(partDataList.size()));
}