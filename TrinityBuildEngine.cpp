// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"
#include <io.h>

// ============================================================
// ГЛАВНЫЙ РЕКУРСИВНЫЙ МЕТОД
// ============================================================
// Логика:
//   1. Загружаем нейрон по коду
//   2. ensureFileExists — кэш текущего прохода / диск / постройка
//   3. attachXref — вставка XREF ребёнка в целевую базу
//   4. markNeuronDone — ТОЛЬКО после успешной вставки XREF
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

    // 1. Загружаем нейрон
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] Neuron not found: %s\n"), wCode);
        free(wCode);
        return AcDbObjectId::kNull;
    }

    TrinityNeuron neuron = *pNeuron;
    delete pNeuron;

    // 2. Готовим файл на диске: кэш текущего прохода -> диск -> постройка.
    //    doneId — id нейрона, файл которого был ПОСТРОЕН заново в этом проходе
    //    (-1, если файл уже был на диске и ничего не строилось).
    int doneId = -1;
    std::string filePath = ensureFileExists(neuron.code, depth, &doneId);
    if (filePath.empty()) return AcDbObjectId::kNull;

    // 3. Вставляем XREF. Пометку "done" переносим ТОЛЬКО после успешной вставки.
    AcDbObjectId xrefId = m_files.attachXref(filePath, neuron.code, position, rotation, targetDb);
    if (xrefId != AcDbObjectId::kNull) {
        if (doneId >= 0) m_core.markNeuronDone(doneId);
    } else {
        wchar_t* wCode = utf2uni(neuron.code.c_str());
        acutPrintf(_T("\n[BuildEngine] attachXref failed for: %s (not marked done)\n"), wCode);
        free(wCode);
    }

    return xrefId;
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
AcDbDatabase* TrinityBuildEngine::buildDetail(const TrinityNeuron& detail, int* outBuiltId) {
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

    pMs2->close();

    // Файл будет сохранён вызывающим кодом, но пометку "done" здесь НЕ ставим —
    // статус переносится только после успешной вставки XREF этого файла.
    if (outBuiltId) *outBuiltId = detail.id;

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
AcDbDatabase* TrinityBuildEngine::buildDwg(const TrinityNeuron& neuron, int depth, int* outBuiltId) {
    if (neuron.type == "detail") {
        return buildDetail(neuron, outBuiltId);
    }

    AcDbDatabase* tempDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
    if (!tempDb) return nullptr;

    AcDbObjectIdArray ids;

    auto children = m_core.loadChildren(neuron.id);

    /*
    wchar_t* wCode = utf2uni(neuron.code.c_str());
    acutPrintf(_T("\n[BuildEngine] Building %s: %d children (depth=%d)\n"),
        wCode, static_cast<int>(children.size()), depth);
    free(wCode);
    */

    // Один и тот же ребёнок может встречаться в children несколько раз
    // (несколько синапсов parent->child, например D.S.2.425.425.10 в двери).
    // Файл строим/сохраняем ОДИН РАЗ, а вставляем столько BlockReference,
    // сколько синапсов — с УНИКАЛЬНЫМИ именами блоков-XREF. Иначе вторая
    // вставка того же имени даёт "Duplicate definition of block ... ignored",
    // ссылка остаётся битой и wblock всей конструкции завершается ошибкой.
    std::map<std::string, int> builtByCode;   // code -> число успешных вставок блока

    for (auto& syn : children) {
        AcGePoint3d childPos = syn.position.toAcGe();

        const int prevCount = builtByCode.count(syn.childCode)
                                  ? builtByCode[syn.childCode] : 0;

        int builtChildId = -1;
        std::string childFilePath;
        if (prevCount == 0) {
            childFilePath = ensureFileExists(syn.childCode, depth + 1, &builtChildId);
        } else {
            // Блок этого файла уже есть в tempDb — файл гарантированно на диске,
            // повторно сохранять его нельзя (заблокирован открытой базой).
            childFilePath = getExistingFilePath(syn.childCode);
        }
        if (childFilePath.empty()) {
            acutPrintf(_T("\n[BuildEngine] ensureFileExists returned EMPTY\n"));
            continue;
        }

        // Имя блока: первый экземпляр — код файла, последующие — код$0, код$1...
        std::string blockName = syn.childCode;
        if (prevCount > 0) blockName += "$" + std::to_string(prevCount - 1);

        // НЕ держим Model Space открытым — attachXref сам его откроет
        AcDbObjectId childId = m_files.attachXref(
            childFilePath, blockName, childPos, syn.rotation, tempDb);

        if (childId != AcDbObjectId::kNull) {
            ids.append(childId);
            builtByCode[syn.childCode] = prevCount + 1;
            // Пометка "done" — ТОЛЬКО после успешной вставки XREF
            if (builtChildId >= 0) {
                m_core.markNeuronDone(builtChildId);
            }
        }
        else {
            wchar_t* wChild = utf2uni(syn.childCode.c_str());
            acutPrintf(_T("\n[BuildEngine] attachXref returned kNull for %s\n"), wChild);
            free(wChild);
        }
    }

    // Получаем Model Space ТОЛЬКО ПОСЛЕ цикла, для закрытия
    // Или вообще не открываем его — wblock сам разберётся
    // (мы не добавляли ничего вручную в Model Space, только через attachXref)

    //acutPrintf(_T("\n[BuildEngine] buildDwg loop done, ids.length=%d\n"), (int)ids.length());

    if (ids.isEmpty()) {
        delete tempDb;
        return nullptr;
    }

    // ВАЖНО: wblock() создаёт НОВУЮ базу-копию и не требует, чтобы исходная
    // база оставалась жива. tempDb удаляем СРАЗУ ПОСЛЕ wblock — иначе её
    // открытые XREF-базы держат дочерние DWG на диске заблокированными,
    // и последующий saveAs() файла конструкции падает (битый/неполный файл,
    // error 320 при следующей вставке).
    AcDbDatabase* cleanDb = nullptr;
    Acad::ErrorStatus es = tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    delete tempDb;
    tempDb = nullptr;

    if (es != Acad::eOk || !cleanDb) {
        delete cleanDb;
        return nullptr;
    }

    // Файл будет создан (сохранён в ensureExists/ensureFileExists), но пометку
    // "done" здесь НЕ ставим — статус переносится только после успешной вставки XREF.
    if (outBuiltId) *outBuiltId = neuron.id;

    return cleanDb;
}

// ============================================================
// ПУТЬ К ФАЙЛУ НЕЙРОНА (по коду) — без проверки существования
// ============================================================
std::string TrinityBuildEngine::getExistingFilePath(const std::string& code) {
    auto itCache = m_builtPaths.find(code);
    if (itCache != m_builtPaths.end()) return itCache->second;

    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return "";

    TrinityNeuron neuron = *pNeuron;
    delete pNeuron;

    std::string subdir;
    if (neuron.type == "detail") {
        subdir = m_files.detailsDir();
    } else if (neuron.type == "assembly" || neuron.type == "construction") {
        subdir = m_files.assembliesDir();
    } else {
        subdir = m_files.projectsDir();
    }

    std::string filePath = m_files.getFilePath(neuron.code, subdir);
    if (m_files.fileExists(neuron.code, subdir)) {
        m_builtPaths[code] = filePath;
        return filePath;
    }
    return "";
}

// ============================================================
// ОБЕСПЕЧИТЬ СУЩЕСТВОВАНИЕ ФАЙЛА (без вставки XREF)
// ============================================================
// Используется внутри buildDwg для рекурсивной подготовки детей.
// Возвращает путь к файлу или пустую строку при ошибке.
// ============================================================
std::string TrinityBuildEngine::ensureFileExists(const std::string& code, int depth, int* doneId) {
    // Кэш файлов, построенных в ТЕКУЩЕМ проходе сборки.
    // Повторное сохранение того же пути через saveAs() недопустимо:
    // база, которая уже держит этот DWG открытым (XREF во временной базе
    // родителя), блокирует файл на диске -> saveAs падает, файл остаётся
    // неполным, а последующая вставка XREF даёт error 320 (eNullEntityHandle).
    auto itCache = m_builtPaths.find(code);
    if (itCache != m_builtPaths.end()) return itCache->second;

    // Защита от бесконечной рекурсии
    if (depth > 20) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] MAX DEPTH for %s\n"), wCode);
        free(wCode);
        return "";
    }

    // Загружаем нейрон
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return "";

    TrinityNeuron neuron = *pNeuron;
    delete pNeuron;

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

    // Если файл уже есть на диске — возвращаем путь (ничего не строим,
    // doneId не заполняем: нейрон уже был готов до этого прохода).
    // В кэш НЕ кладём: такой файл ничем не заблокирован и при необходимости
    // может быть перезаписан корректно.
    if (m_files.fileExists(neuron.code, subdir)) return filePath;

    // Строим
    AcDbDatabase* db = nullptr;
    if (neuron.type == "detail") {
        db = buildDetail(neuron, doneId);
    }
    else {
        db = buildDwg(neuron, depth, doneId);
    }

    if (!db) return "";

    // Сохраняем. Если сохранение не удалось — файл ненадёжен:
    // НЕ кладём его в кэш, чтобы следующий запрос попробовал построить
    // и сохранить заново. Статус "done" при этом не ставится — это делает
    // вызывающий код только после успешной вставки XREF.
    if (!m_files.saveDwg(db, filePath)) {
        delete db;
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] saveDwg failed for %s\n"), wCode);
        free(wCode);
        return "";
    }
    delete db;
    Sleep(200);

    m_builtPaths[code] = filePath;
    return filePath;
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
        // Новый проход сборки — сбрасываем кэш построенных файлов
        resetBuildCache();

        // Рекурсивно удаляем все файлы, связанные с этим проектом
        // Это гарантирует, что сборка начнётся с чистого листа
        deleteProjectFiles(proj.code);

        // Создаём файл проекта (рекурсивно). builtId != -1, если файл был построен заново.
        int builtId = -1;
        std::string actualPath = ensureFileExists(proj.code, 0, &builtId);

        if (actualPath.empty()) {
            wchar_t* wCode = utf2uni(proj.code.c_str());
            acutPrintf(_T("\n[BuildEngine] Failed to create project: %s\n"), wCode);
            free(wCode);
            continue;
        }

        // Вставляем XREF готового файла проекта в целевую базу.
        // Пометку "done" переносим ТОЛЬКО после успешной вставки XREF,
        // а не сразу после создания файла.
        AcDbObjectId xrefId = m_files.attachXref(
            actualPath, proj.code, AcGePoint3d::kOrigin,
            TrinityRotationCompound(), targetDb);

        if (xrefId == AcDbObjectId::kNull) {
            wchar_t* wCode = utf2uni(proj.code.c_str());
            acutPrintf(_T("\n[BuildEngine] attachXref failed for project: %s (not marked done)\n"), wCode);
            free(wCode);
            continue;
        }

        // XREF вставлен успешно — теперь можно отмечать done
        // (для только что построенных файлов; id уже помеченных/существующих = -1)
        if (builtId >= 0) {
            m_core.markNeuronDone(builtId);
        }

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

    // Определяем подкаталог и путь к файлу
    std::string subdir;
    if (neuron.type == "detail") {
        subdir = m_files.detailsDir();
    } else if (neuron.type == "assembly" || neuron.type == "construction") {
        subdir = m_files.assembliesDir();
    } else {
        subdir = m_files.projectsDir();
    }

    std::string filePath = m_files.getFilePath(neuron.code, subdir);

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