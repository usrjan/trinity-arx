// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"
#include <io.h>

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
    if (tempDb->getSymbolTable(pBt, AcDb::kForRead) != Acad::eOk) {
        delete tempDb;
        return nullptr;
    }
    AcDbBlockTableRecord* pMs = nullptr;
    if (pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite) != Acad::eOk) {
        pBt->close();
        delete tempDb;
        return nullptr;
    }
    pBt->close();

    AcDbObjectIdArray ids;

    // Добавляем солид (но НЕ закрываем — нужен для booleanOper)
    AcDbObjectId solidId;
    pMs->appendAcDbEntity(solidId, solid);

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

            Acad::ErrorStatus es = solid->booleanOper(AcDb::kBoolSubtract, pCylinder);

            // После booleanOper цилиндр либо NULL solid, либо невалиден
            // Освобождаем память — НЕ вызываем erase()
            delete pCylinder;
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
    TrinityAttributeBuilder::ensureTagLayer(tempDb);
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
    delete tempDb;

    // При ошибке wblock гарантированно возвращает nullptr в cleanDb —
    // повторное delete было бы двойным освобождением
    if (es != Acad::eOk || !cleanDb) {
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
    if (cleanDb->getSymbolTable(pBt2, AcDb::kForRead) == Acad::eOk) {
        AcDbBlockTableRecord* pMs2 = nullptr;
        if (pBt2->getAt(ACDB_MODEL_SPACE, pMs2, AcDb::kForWrite) == Acad::eOk) {
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
                            pEnt->setLayer(matLayerW);       // солид → материал
                        } else if (pEnt->isKindOf(AcDbCircle::desc())) {
                            pEnt->setLayer(_T("_bolt"));     // кружочек → _bolt
                        } else {
                            pEnt->setLayer(_T("_tag"));      // атрибут → _tag
                        }
                        pEnt->close();
                    }
                }
                delete pIter;
            }

            pMs2->close();
        } else {
            pBt2->close();
        }
    }

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
//      - ensureFileExists — убедиться, что файл есть (без XREF)
//      - attachXref — вставить XREF ребёнка во ВРЕМЕННУЮ базу
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

    /*
    wchar_t* wCode = utf2uni(neuron.code.c_str());
    acutPrintf(_T("\n[BuildEngine] Building %s: %d children (depth=%d)\n"),
        wCode, static_cast<int>(children.size()), depth);
    free(wCode);
    */

    for (auto& syn : children) {
        AcGePoint3d childPos = syn.position.toAcGe();

        /*
        wchar_t* wChild = utf2uni(syn.childCode.c_str());
        acutPrintf(_T("\n[BuildEngine] Child: %s (depth=%d)\n"), wChild, depth);
        free(wChild);
        */

        std::string childFilePath = ensureFileExists(syn.childCode, depth + 1);
        if (childFilePath.empty()) {
            acutPrintf(_T("\n[BuildEngine] ensureFileExists returned EMPTY\n"));
            continue;
        }

        //acutPrintf(_T("\n[BuildEngine] Got file path, attaching XREF...\n"));

        // НЕ держим Model Space открытым — attachXref сам его откроет
        AcDbObjectId childId = m_files.attachXref(
            childFilePath, syn.childCode, childPos, syn.rotation, tempDb);

        if (childId != AcDbObjectId::kNull) {
            ids.append(childId);
            //acutPrintf(_T("\n[BuildEngine] XREF appended to ids\n"));
        }
        else {
            acutPrintf(_T("\n[BuildEngine] attachXref returned kNull\n"));
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

    AcDbDatabase* cleanDb = nullptr;
    Acad::ErrorStatus es = tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    delete tempDb;

    // При ошибке wblock гарантированно возвращает nullptr в cleanDb —
    // повторное delete было бы двойным освобождением
    if (es != Acad::eOk || !cleanDb) {
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

    // Если файл уже есть — возвращаем путь
    if (m_files.fileExists(neuron.code, subdir)) return filePath;

    // Строим
    AcDbDatabase* db = nullptr;
    if (neuron.type == "detail") {
        db = buildDetail(neuron);
    }
    else {
        db = buildDwg(neuron, depth);
    }

    if (!db) return "";

    // Сохраняем
    m_files.saveDwg(db, filePath);
    delete db;
    Sleep(200);

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
// ============================================================
// ОТРИСОВКА ДЕТАЛЕЙ КАТЕГОРИИ В ТЕКУЩИЙ ЧЕРТЕЖ (TRIB)
// ============================================================
// Для каждой детали:
//   1. Блок TRIB_<code> в таблицу блоков чертежа (переиспользуется
//      при повторном вызове — блок перезаписывается геометрией).
//   2. Внутри блока: солид + маркеры болтов (для rib) + невидимый
//      текст с кодом (для поиска).
//   3. XDATA приложения TRINITY (REGAPPID): категория, код, материал,
//      базовые Width/Height/Thickness — метаданные для панелей свойств.
//   4. AcDbBlockReference в ModelSpace со стандартными параметрическими
//      свойствами (Height/Width/Rotation/State) — видны в палитре
//      "Свойства" как у обычного блока.
// ============================================================
int TrinityBuildEngine::drawDetailsInDrawing(AcDbDatabase* targetDb,
                                             const std::string& category) {
    if (!targetDb) return 0;

    auto details = m_core.loadDetailsByCategory(category);
    if (details.empty()) {
        acutPrintf(_T("\n[Trinity] No '%hs' details found in database.\n"), category.c_str());
        return 0;
    }

    // Регистрируем приложение для XDATA
    {
        resbuf* pRegApp = acutNewRb(AcDb::kDxfRegAppName);
        wcscpy_s(pRegApp->resval.rstring, 256, L"TRINITY");
        pRegApp->rbnext = nullptr;
        AcDbRegAppTable* pRegApps = nullptr;
        if (targetDb->getSymbolTable(pRegApps, AcDb::kForWrite) == Acad::eOk) {
            if (!pRegApps->has(L"TRINITY"))
                pRegApps->add(pRegApp);
            pRegApps->close();
        }
        acutRelRb(pRegApp);
    }

    AcDbBlockTable* pBT = nullptr;
    if (targetDb->getSymbolTable(pBT, AcDb::kForWrite) != Acad::eOk) return 0;

    AcDbBlockTableRecord* pMS = nullptr;
    if (pBT->getAt(ACDB_MODEL_SPACE, pMS, AcDb::kForWrite) != Acad::eOk) {
        pBT->close();
        return 0;
    }

    double yCursor = 0.0;               // раскладка деталей по Y
    const double ROW_GAP = 80.0;
    int inserted = 0;

    for (const auto& d : details) {
        if (d.width <= 0 || d.height <= 0 || d.thickness <= 0) continue;

        // ---- имя блока ----
        wchar_t blockName[256];
        _snwprintf_s(blockName, 256, _T("TRIB_%hs"), d.code.c_str());

        // ---- создать или очистить существующий блок ----
        AcDbBlockTableRecord* pBtr = nullptr;
        if (pBT->getAt(blockName, pBtr, AcDb::kForWrite) == Acad::eOk) {
            // перезапись: удаляем старую геометрию
            AcDbObjectIdArray toErase;
            for (AcDbBlockTableRecordIterator* pIt = nullptr;
                 pBtr->newIterator(pIt) == Acad::eOk; ) {
                for (; !pIt->done(); pIt->step()) {
                    AcDbObjectId id;
                    if (pIt->getEntityId(id) == Acad::eOk) toErase.append(id);
                }
                delete pIt;
                break;
            }
            for (int i = 0; i < toErase.length(); i++) {
                AcDbEntity* pEnt = nullptr;
                if (acdbOpenObject(pEnt, toErase[i], AcDb::kForWrite) == Acad::eOk) {
                    pEnt->erase();
                    pEnt->close();
                }
            }
        } else {
            pBtr = new AcDbBlockTableRecord();
            pBtr->setName(blockName);
            if (pBT->add(pBtr) != Acad::eOk) {
                delete pBtr;
                continue;
            }
            pBtr->setOrigin(AcGePoint3d::kOrigin);
        }

        // ---- геометрия внутри блока ----
        AcDb3dSolid* solid = TrinityGeometryBuilder::build(d);
        if (solid) {
            AcDbObjectId entId;
            if (pBtr->appendAcDbEntity(entId, solid) == Acad::eOk)
                solid->close();
            else
                delete solid;
        }

        if (category == "rib") {
            TrinityLayerManager::ensureBoltLayer(targetDb);
            AcDbObjectIdArray ids;
            TrinityGeometryBuilder::drawBoltMarkers(d, pBtr, ids);
        }

        // невидимая метка с кодом детали (слой _tag)
        TrinityAttributeBuilder::ensureTagLayer(targetDb);
        AcDbText* pTag = new AcDbText();
        wchar_t codeW[256];
        MultiByteToWideChar(CP_UTF8, 0, d.code.c_str(), -1, codeW, 256);
        pTag->setPosition(AcGePoint3d::kOrigin);
        pTag->setTextString(codeW);
        pTag->setHeight(10);
        pTag->setLayer(_T("_tag"));
        pTag->setInvisibility(Adesk::kTrue);
        AcDbObjectId tagId;
        if (pBtr->appendAcDbEntity(tagId, pTag) == Acad::eOk)
            pTag->close();
        else
            delete pTag;

        // ---- XDATA: метаданные детали на определении блока ----
        resbuf* xd = nullptr;
        auto addStr = [&xd](int code, const wchar_t* val) {
            resbuf* rb = acutNewRb(code);
            rb->rbnext = nullptr;
            wcsncpy_s(rb->resval.rstring, 256, val, _TRUNCATE);
            xd = acutAppendRb(xd, rb);
        };
        addStr(AcDb::kDxfRegAppName,  L"TRINITY");
        addStr(AcDb::kDxfXdAsciiString, L"CATEGORY"); // далее пары ключ-значение
        wchar_t buf[256];
        _snwprintf_s(buf, 256, _T("%hs"), category.c_str());
        addStr(AcDb::kDxfXdAsciiString, buf);
        addStr(AcDb::kDxfXdAsciiString, L"CODE");
        addStr(AcDb::kDxfXdAsciiString, codeW);
        addStr(AcDb::kDxfXdAsciiString, L"MATERIAL");
        wchar_t matW[256];
        MultiByteToWideChar(CP_UTF8, 0, d.material.c_str(), -1, matW, 256);
        addStr(AcDb::kDxfXdAsciiString, matW);
        addStr(AcDb::kDxfXdAsciiString, L"BASE_WIDTH");
        _snwprintf_s(buf, 256, _T("%.1f"), (double)d.width);
        addStr(AcDb::kDxfXdAsciiString, buf);
        addStr(AcDb::kDxfXdAsciiString, L"BASE_HEIGHT");
        _snwprintf_s(buf, 256, _T("%.1f"), (double)d.height);
        addStr(AcDb::kDxfXdAsciiString, buf);
        addStr(AcDb::kDxfXdAsciiString, L"BASE_THICKNESS");
        _snwprintf_s(buf, 256, _T("%.1f"), (double)d.thickness);
        addStr(AcDb::kDxfXdAsciiString, buf);
        pBtr->xData(xd, true);
        acutRelRb(xd);

        pBtr->close();

        // ---- ссылка на блок в ModelSpace ----
        AcDbBlockTableRecord* pBtrR = nullptr;
        if (pBT->getAt(blockName, pBtrR, AcDb::kForRead) == Acad::eOk) {
            AcDbBlockReference* pRef = new AcDbBlockReference(
                AcGePoint3d(0.0, yCursor, 0.0), pBtrR->objectId());
            pBtrR->close();

            AcDbObjectId refId;
            if (pMS->appendAcDbEntity(refId, pRef) == Acad::eOk) {
                pRef->close();
                inserted++;
            } else {
                delete pRef;
            }
        }

        yCursor += d.height + ROW_GAP;
    }

    pMS->close();
    pBT->close();

    acutPrintf(_T("\n[Trinity] Inserted %d '%hs' detail(s) into current drawing.\n"),
               inserted, category.c_str());
    return inserted;
}
