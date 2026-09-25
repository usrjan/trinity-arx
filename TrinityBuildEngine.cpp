// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"
#include <io.h>

// ============================================================
// ОБЩИЕ ПРАВИЛА ПУТЕЙ (вынесено из ensureFileExists / deleteProjectFiles)
// ============================================================
// Тип нейрона → подкаталог хранилища DWG
std::string TrinityBuildEngine::subdirForType(const std::string& type) {
    if (type == "detail") {
        return m_files.detailsDir();
    }
    if (type == "assembly" || type == "construction") {
        return m_files.assembliesDir();
    }
    return m_files.projectsDir();
}

// Полный путь к DWG-файлу нейрона
std::string TrinityBuildEngine::filePathForNeuron(const TrinityNeuron& neuron) {
    return m_files.getFilePath(neuron.code, subdirForType(neuron.type));
}

// ============================================================
// ОБЕСПЕЧИТЬ СУЩЕСТВОВАНИЕ ФАЙЛА (без вставки XREF)
// ============================================================
// Единая точка загрузки нейрона и постройки файла:
//   1. Проверка глубины рекурсии
//   2. Загрузка нейрона по коду
//   3. Путь через общие правила (filePathForNeuron)
//   4. Если файла нет — buildDetail / buildDwg + saveDwg
// Возвращает путь к файлу или пустую строку при ошибке.
// Используется в buildDwg для детей и в processAllProjects для проектов.
// ============================================================
std::string TrinityBuildEngine::ensureFileExists(const std::string& code, int depth) {
    // Защита от бесконечной рекурсии
    if (depth > 20) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] MAX DEPTH for %s\n"), wCode);
        free(wCode);
        return "";
    }

    // Загружаем нейрон
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] Neuron not found: %s\n"), wCode);
        free(wCode);
        return "";
    }

    TrinityNeuron neuron = *pNeuron;
    delete pNeuron;

    std::string filePath = filePathForNeuron(neuron);

    // Если файл уже есть — возвращаем путь
    if (m_files.fileExists(neuron.code, subdirForType(neuron.type))) return filePath;

    // Строим
    wchar_t* wCode = utf2uni(neuron.code.c_str());
    wchar_t* wType = utf2uni(neuron.type.c_str());
    acutPrintf(_T("\n[BuildEngine] BUILDING: %s (type=%s, depth=%d)\n"),
               wCode, wType, depth);
    free(wCode);
    free(wType);

    AcDbDatabase* db = nullptr;
    if (neuron.type == "detail") {
        db = buildDetail(neuron);
    } else {
        db = buildDwg(neuron, depth);
    }

    if (!db) return "";

    // Сохраняем
    m_files.saveDwg(db, filePath);
    delete db;

    // Пауза для файловой системы
    Sleep(200);

    return filePath;
}

// ============================================================
// РЕКУРСИВНОЕ ОБЕСПЕЧЕНИЕ DWG + ВСТАВКА XREF
// ============================================================
// Вся логика «есть ли файл / построить» объединена в ensureFileExists —
// здесь остаётся только надстройка «вставить XREF в целевую базу».
// position == AcGePoint3d::kOrigin (nullPos) — режим «только файл»:
// постройка выполняется, вставка пропускается (возвращается kNull).
// ============================================================
AcDbObjectId TrinityBuildEngine::ensureExists(const std::string& code,
                                                const AcGePoint3d& position,
                                                const TrinityRotationCompound& rotation,
                                                AcDbDatabase* targetDb,
                                                int depth) {
    const std::string filePath = ensureFileExists(code, depth);
    if (filePath.empty()) return AcDbObjectId::kNull;

    // Режим «нужен только файл» (используется рекурсией buildDwg)
    if (position.isEqualTo(AcGePoint3d::kOrigin)) {
        return AcDbObjectId::kNull;
    }

    if (depth > 20) return AcDbObjectId::kNull; // защита от глубокой вставки

    wchar_t* wCode = utf2uni(code.c_str());
    acutPrintf(_T("\n[BuildEngine] ATTACH XREF: %s (depth=%d)\n"), wCode, depth);
    free(wCode);

    return m_files.attachXref(filePath, code, position, rotation, targetDb);
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
//  10. Переназначение слоёв по общим правилам (applyDetailLayerRules):
//      solid → слой материала, circle → _bolt, остальное → _tag
// ============================================================

// ----------------------------------------------------------------
// ОБЩИЕ ПРАВИЛА СЛОЁВ ДЛЯ ДЕТАЛИ: сущность → целевой слой
// solid   → слой материала, circle → _bolt (болты), прочее → _tag (атрибуты)
// ----------------------------------------------------------------
static void applyDetailLayerRule(AcDbEntity* pEnt, const wchar_t* materialLayerW) {
    if (pEnt->isKindOf(AcDb3dSolid::desc())) {
        pEnt->setLayer(materialLayerW);                      // солид → материал
    } else if (pEnt->isKindOf(AcDbCircle::desc())) {
        pEnt->setLayer(TrinityLayerManager::LAYER_BOLT_W);   // кружочек → _bolt
    } else {
        pEnt->setLayer(TrinityLayerManager::LAYER_TAG_W);    // атрибут → _tag
    }
}

AcDbDatabase* TrinityBuildEngine::buildDetail(const TrinityNeuron& detail) {
    AcDbDatabase* tempDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
    if (!tempDb) return nullptr;

    // Слой материала
    TrinityLayerManager::createOrGetLayer(tempDb, detail.material);

    // Строим геометрию
    AcDb3dSolid* solid = TrinityGeometryBuilder::build(detail);
    if (!solid) {
        delete tempDb;
        return nullptr;
    }

    // Назначаем слой
    std::string layer = TrinityLayerManager::layerName(detail.material);
    wchar_t layerW[256];
    MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, layerW, 256);
    solid->setLayer(layerW);

    // Получаем Model Space
    AcDbBlockTable* pBt = nullptr;
    Acad::ErrorStatus esBt = tempDb->getSymbolTable(pBt, AcDb::kForRead);
    if (esBt != Acad::eOk || !pBt) {
        acutPrintf(_T("\n[BuildEngine] getSymbolTable failed: %d\n"), (int)esBt);
        delete solid;
        delete tempDb;
        return nullptr;
    }

    AcDbBlockTableRecord* pMs = nullptr;
    Acad::ErrorStatus esMs = pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBt->close();  // закрываем таблицу сразу — она больше не нужна

    if (esMs != Acad::eOk || !pMs) {
        acutPrintf(_T("\n[BuildEngine] getModelSpace failed: %d\n"), (int)esMs);
        // ВАЖНО: закрыть частично открытый BTR перед удалением базы —
        // удаление AcDbDatabase с открытыми объектами = Access Violation.
        if (pMs) pMs->close();
        // solid ещё НЕ добавлен в базу — его можно и нужно удалить напрямую.
        delete solid;
        delete tempDb;
        return nullptr;
    }

    AcDbObjectIdArray ids;

    // Добавляем солид (но НЕ закрываем — нужен для booleanOper)
    AcDbObjectId solidId;
    Acad::ErrorStatus esApp = pMs->appendAcDbEntity(solidId, solid);
    if (esApp != Acad::eOk) {
        acutPrintf(_T("\n[BuildEngine] appendAcDbEntity(solid) failed: %d\n"), (int)esApp);
        solid->close();
        pMs->close();
        // ВАЖНО (фикс Access Violation): delete на объекте AcDbObject, уже
        // приписанном к базе, запрещён документацией ObjectARX. Если append
        // частично состоялся (solidId валиден) — удаляем через erase(),
        // иначе объект будет освобождён вместе с tempDb.
        if (solidId.isValid()) {
            solid->erase();
        } else {
            delete solid;
        }
        delete tempDb;
        return nullptr;
    }

    // ============================================================
    // БОЛТЫ ДЛЯ ПЛАНОК
    // ============================================================
    if (detail.category == "rib") {
        TrinityLayerManager::ensureBoltLayer(tempDb);
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
            AcDb3dSolid* pCylinder = new AcDb3dSolid();
            if (!pCylinder) continue;
            pCylinder->createFrustum(height, holeRadius, holeRadius, holeRadius);

            AcGeMatrix3d mat;
            mat.setToIdentity();
            mat.setTranslation(AcGeVector3d(pos.x, pos.y, detail.thickness / 2.0));
            pCylinder->transformBy(mat);

            Acad::ErrorStatus esBool = solid->booleanOper(AcDb::kBoolSubtract, pCylinder);
            if (esBool != Acad::eOk) {
                acutPrintf(_T("\n[BuildEngine] booleanOper failed: %d\n"), (int)esBool);
            }

            // После booleanOper цилиндр либо NULL solid, либо невалиден
            // Освобождаем память — НЕ вызываем erase()
            delete pCylinder;
        }
    }

    // ============================================================
    // ЗАКРЫВАЕМ СОЛИД ПОСЛЕ ВСЕХ ОПЕРАЦИЙ
    // ============================================================
    Acad::ErrorStatus esSolidClose = solid->close();
    if (esSolidClose != Acad::eOk) {
        acutPrintf(_T("\n[BuildEngine] solid->close() failed: %d\n"), (int)esSolidClose);
    }
    ids.append(solidId);

    // ============================================================
    // АТРИБУТЫ (DETAIL_CODE на слое _tag)
    // ============================================================
    TrinityAttributeBuilder::ensureTagLayer(tempDb);
    AcDbObjectId attrId = TrinityAttributeBuilder::addDetailCode(pMs, detail.code);
    if (attrId != AcDbObjectId::kNull) {
        ids.append(attrId);
    }

    // Model Space закрываем ПОСЛЕДНИМ — все сущности уже закрыты
    Acad::ErrorStatus esMsClose = pMs->close();
    if (esMsClose != Acad::eOk) {
        acutPrintf(_T("\n[BuildEngine] pMs->close() failed: %d\n"), (int)esMsClose);
    }

    // ============================================================
    // WBLOCK → чистая база
    // ============================================================
    AcDbDatabase* cleanDb = nullptr;
    Acad::ErrorStatus es = tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    delete tempDb;

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
    Acad::ErrorStatus esBt2 = cleanDb->getSymbolTable(pBt2, AcDb::kForRead);
    if (esBt2 != Acad::eOk || !pBt2) {
        acutPrintf(_T("\n[BuildEngine] getSymbolTable(cleanDb) failed: %d\n"), (int)esBt2);
        delete cleanDb;
        return nullptr;
    }

    AcDbBlockTableRecord* pMs2 = nullptr;
    Acad::ErrorStatus esMs2 = pBt2->getAt(ACDB_MODEL_SPACE, pMs2, AcDb::kForWrite);
    pBt2->close();  // таблицу закрываем сразу, до работы с BTR

    if (esMs2 != Acad::eOk || !pMs2) {
        acutPrintf(_T("\n[BuildEngine] getModelSpace(cleanDb) failed: %d\n"), (int)esMs2);
        // ВАЖНО: если getAt частично открыл BTR (esMs != eOk, но pMs2 != nullptr),
        // обязательно закрыть её ПЕРЕД delete cleanDb — иначе удаление базы
        // с открытым объектом вызывает Fatal Error / Access Violation.
        if (pMs2) pMs2->close();
        delete cleanDb;
        return nullptr;
    }

    AcDbBlockTableRecordIterator* pIter = nullptr;
    Acad::ErrorStatus esIter = pMs2->newIterator(pIter);
    if (esIter != Acad::eOk || !pIter) {
        acutPrintf(_T("\n[BuildEngine] newIterator(cleanDb) failed: %d\n"), (int)esIter);
        pMs2->close();  // close() перед delete — иначе открытая BTR утечёт при удалении базы
        delete cleanDb;
        return nullptr;
    }

    wchar_t matLayerW[256];
    MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, matLayerW, 256);

    // Общие правила слоёв — одна функция applyDetailLayerRule на сущность
    for (pIter->start(); !pIter->done(); pIter->step()) {
        AcDbEntity* pEnt = nullptr;
        if (pIter->getEntity(pEnt, AcDb::kForWrite) == Acad::eOk && pEnt) {
            applyDetailLayerRule(pEnt, matLayerW);
            pEnt->close();
        }
    }
    delete pIter;

    pMs2->close();

    // ВАЖНО: у AcDbDatabase нет discardEditing()/close() (это методы AcDbObject,
    // см. dbmain.h). Флаг "editing" side-database блокировок *.dwl/*.dwl2 не
    // создаёт; saveAs()/delete корректно освобождают ресурсы базы.

    // Финальный отчёт
    /*
    wchar_t* wCode = utf2uni(detail.code.c_str());
    acutPrintf(_T("\n[BuildEngine] Detail built: %s\n"), wCode);
    free(wCode);
    */

    return cleanDb;
}

// ============================================================
// ПОСТРОЕНИЕ КОНСТРУКЦИИ/ПРОЕКТА
// ============================================================
// Логика:
//   1. tempDb — временная база
//   2. Для каждого ребёнка:
//      - ensureExists(position=nullPos) — построить файл (без вставки),
//        затем attachXref во ВРЕМЕННУЮ базу (общая логика с ensureExists)
//      - собрать ObjectId в массив
//   3. wblock → cleanDb
// ============================================================
AcDbDatabase* TrinityBuildEngine::buildDwg(const TrinityNeuron& neuron, int depth) {
    if (neuron.type == "detail") {
        return buildDetail(neuron);
    }

    AcDbDatabase* tempDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
    if (!tempDb) return nullptr;

    AcDbObjectIdArray ids;

    auto children = m_core.loadChildren(neuron.id);

    for (auto& syn : children) {
        AcGePoint3d childPos = syn.position.toAcGe();

        // Общие правила пути + постройка файла (если нужно) — внутри ensureFileExists
        std::string childFilePath = ensureFileExists(syn.childCode, depth + 1);
        if (childFilePath.empty()) {
            acutPrintf(_T("\n[BuildEngine] ensureFileExists returned EMPTY\n"));
            continue;
        }

        // НЕ держим Model Space открытым — attachXref сам его откроет
        AcDbObjectId childId = m_files.attachXref(
            childFilePath, syn.childCode, childPos, syn.rotation, tempDb);

        if (childId != AcDbObjectId::kNull) {
            ids.append(childId);
        }
        else {
            acutPrintf(_T("\n[BuildEngine] attachXref returned kNull\n"));
        }
    }

    if (ids.isEmpty()) {
        delete tempDb;
        return nullptr;
    }

    AcDbDatabase* cleanDb = nullptr;
    Acad::ErrorStatus es = tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    delete tempDb;

    if (es != Acad::eOk || !cleanDb) {
        delete cleanDb;
        return nullptr;
    }

    // ВАЖНО: у AcDbDatabase нет discardEditing()/close() (см. dbmain.h).
    // Side-database из wblock() не создаёт *.dwl/*.dwl2 — блокировку снимает
    // saveDwg() (freeThreadedData) либо деструктор при delete.

    return cleanDb;
}

// ============================================================
// ОБРАБОТКА ВСЕХ PENDING-ПРОЕКТОВ
// ============================================================
int TrinityBuildEngine::processAllProjects(AcDbDatabase* targetDb) {
    auto projects = m_core.loadPendingProjects();
    if (projects.empty()) return 0;

    /*
    acutPrintf(_T("\n[BuildEngine] Processing %d projects...\n"),
        static_cast<int>(projects.size()));
    */

    for (auto& proj : projects) {
        // Рекурсивно удаляем все файлы, связанные с этим проектом
        // Это гарантирует, что сборка начнётся с чистого листа
        deleteProjectFiles(proj.code);

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
// УДАЛЕНИЕ ФАЙЛОВ ПРОЕКТА (рекурсивно по детям)
// ============================================================
void TrinityBuildEngine::deleteProjectFiles(const std::string& code) {
    // Загружаем нейрон
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return;

    TrinityNeuron neuron = *pNeuron;
    delete pNeuron;

    // Общие правила путей (те же, что в ensureFileExists)
    const std::string subdir   = subdirForType(neuron.type);
    const std::string filePath = filePathForNeuron(neuron);

    // Удаляем файл, если он существует
    if (m_files.fileExists(neuron.code, subdir)) {
        wchar_t pathW[512];
        MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, pathW, 512);
        _wunlink(pathW);
        
        wchar_t* wCode = utf2uni(neuron.code.c_str());
        acutPrintf(_T("\n[BuildEngine] Deleted file: %s.dwg\n"), wCode);
        free(wCode);
    }

    // Если это не деталь — рекурсивно удаляем детей
    if (neuron.type != "detail") {
        auto children = m_core.loadChildren(neuron.id);
        for (auto& syn : children) {
            deleteProjectFiles(syn.childCode);
        }
    }
}