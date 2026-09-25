// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"

namespace {

// Диаметр отверстия боковой стенки: Ø8 мм, цилиндр чуть толще стенки
constexpr double HOLE_RADIUS = 4.0;

// Открыть Model Space для записи
AcDbBlockTableRecord* openModelSpace(AcDbDatabase* db) {
    AcDbBlockTable* pBT = nullptr;
    if (db->getSymbolTable(pBT, AcDb::kForRead) != Acad::eOk) return nullptr;
    AcDbBlockTableRecord* pMs = nullptr;
    const Acad::ErrorStatus es = pBT->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBT->close();
    return es == Acad::eOk ? pMs : nullptr;
}

// wblock с корректным освобождением tempDb и результата
AcDbDatabase* wblockOrDeleteTemp(AcDbDatabase* tempDb,
                                 const AcDbObjectIdArray& ids) {
    AcDbDatabase* cleanDb = nullptr;
    const Acad::ErrorStatus es =
        tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin);
    delete tempDb;
    if (es != Acad::eOk || !cleanDb) {
        delete cleanDb;   // безопасно при nullptr
        return nullptr;
    }
    return cleanDb;
}

} // namespace

// ============================================
// Подкаталог и путь для кода нейрона
// ============================================
static std::string subdirOf(TrinityFileManager& files, const TrinityNeuron& n) {
    return TrinityFileManager::subdirForType(n.type);
}

// ============================================
// ГЛАВНЫЙ РЕКУРСИВНЫЙ МЕТОД
// ============================================
AcDbObjectId TrinityBuildEngine::ensureExists(const std::string& code,
                                              const AcGePoint3d& position,
                                              const TrinityRotationCompound& rotation,
                                              AcDbDatabase* targetDb,
                                              int depth) {
    if (depth > MAX_DEPTH) {
        trinityLog(L"[BuildEngine] MAX DEPTH reached for %s", toWide(code).c_str());
        return AcDbObjectId::kNull;
    }

    TrinityNeuronPtr pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) {
        trinityLog(L"[BuildEngine] Neuron not found: %s", toWide(code).c_str());
        return AcDbObjectId::kNull;
    }

    const std::string subdir   = subdirOf(m_files, *pNeuron);
    const std::string filePath = m_files.getFilePath(pNeuron->code, subdir);

    // Файла нет — строим и сохраняем
    if (!m_files.fileExists(pNeuron->code, subdir)) {
        trinityLog(L"[BuildEngine] BUILDING: %s (type=%s, depth=%d)",
                   toWide(pNeuron->code).c_str(),
                   toWide(pNeuron->type).c_str(), depth);

        std::unique_ptr<AcDbDatabase> db(
            pNeuron->type == "detail" ? buildDetail(*pNeuron)
                                      : buildDwg(*pNeuron, depth));
        if (!db) return AcDbObjectId::kNull;

        m_files.saveDwg(db.get(), filePath);
        Sleep(200);   // пауза для файловой системы
    }

    return m_files.attachXref(filePath, pNeuron->code, position, rotation, targetDb);
}

// ============================================
// ПОСТРОЕНИЕ ДЕТАЛИ (щит / планка / стенка)
// ============================================
// tempDb: солид + болты + атрибут → wblock → чистая база,
// в которой заново создаются слои и переназначаются объекты.
// ============================================
AcDbDatabase* TrinityBuildEngine::buildDetail(const TrinityNeuron& detail) {
    std::unique_ptr<AcDbDatabase> tempDb(TrinityFileManager::createEmptyDwg());
    if (!tempDb) return nullptr;

    TrinityLayerManager::createOrGetLayer(tempDb.get(), detail.material);
    TrinityLayerManager::ensureTagLayer(tempDb.get());
    if (detail.category == "rib") TrinityLayerManager::ensureBoltLayer(tempDb.get());

    AcDbBlockTableRecord* pMs = openModelSpace(tempDb.get());
    if (!pMs) return nullptr;

    AcDbObjectIdArray ids;

    // ---- Солид --------------------------------------------------------------
    std::unique_ptr<AcDb3dSolid> solid(TrinityGeometryBuilder::build(detail));
    if (!solid) { pMs->close(); return nullptr; }

    solid->setLayer(toWide(TrinityLayerManager::layerName(detail.material)).c_str());

    AcDbObjectId solidId;
    if (pMs->appendAcDbEntity(solidId, solid.get()) != Acad::eOk) {
        pMs->close();
        return nullptr;
    }
    solid.release();   // теперь владеет база

    // ---- Отверстия боковой стенки ------------------------------------------
    if (detail.category == "sidewall") {
        AcDb3dSolid* pSolid = nullptr;
        if (acdbOpenObject(pSolid, solidId, AcDb::kForWrite) == Acad::eOk) {
            for (const auto& pos : TrinityGeometryBuilder::getHolePositions(detail)) {
                std::unique_ptr<AcDb3dSolid> cyl(new AcDb3dSolid());
                const double h = detail.thickness + 2.0;
                if (cyl->createFrustum(h, HOLE_RADIUS, HOLE_RADIUS, HOLE_RADIUS) != Acad::eOk)
                    continue;
                AcGeMatrix3d mat;
                mat.setToTranslation(AcGeVector3d(pos.x, pos.y, detail.thickness / 2.0 - h / 2.0));
                cyl->transformBy(mat);
                pSolid->booleanOper(AcDb::kBoolSubtract, cyl.get());
                // cyl не в базе — unique_ptr освободит память
            }
            pSolid->close();
        }
    }

    // ---- Маркеры болтов (для щитов и стенок, к которым крепятся планки) ----
    if (detail.category == "shield" || detail.category == "sidewall") {
        TrinityGeometryBuilder::drawBoltMarkers(detail, pMs, ids);
    }

    // ---- Атрибут DETAIL_CODE ------------------------------------------------
    const AcDbObjectId attrId =
        TrinityAttributeBuilder::addDetailCode(pMs, detail.code);
    if (attrId != AcDbObjectId::kNull) ids.append(attrId);

    ids.append(solidId);
    pMs->close();

    // ---- wblock -------------------------------------------------------------
    AcDbDatabase* cleanDb = wblockOrDeleteTemp(tempDb.release(), ids);
    if (!cleanDb) return nullptr;

    // Слои в чистой базе (wblock уносит только используемые)
    TrinityLayerManager::createOrGetLayer(cleanDb, detail.material);
    TrinityLayerManager::ensureTagLayer(cleanDb);
    if (detail.category == "rib") TrinityLayerManager::ensureBoltLayer(cleanDb);

    // Переназначение слоёв: solid→материал, circle→_bolt, прочее→_tag
    const std::wstring matLayerW =
        toWide(TrinityLayerManager::layerName(detail.material));

    AcDbBlockTableRecord* pMs2 = openModelSpace(cleanDb);
    if (pMs2) {
        AcDbBlockTableRecordIterator* pIter = nullptr;
        if (pMs2->newIterator(pIter) == Acad::eOk && pIter) {
            for (; !pIter->done(); pIter->step()) {
                AcDbEntity* pEnt = nullptr;
                if (pIter->getEntity(pEnt, AcDb::kForWrite) == Acad::eOk && pEnt) {
                    if (pEnt->isKindOf(AcDb3dSolid::desc()))
                        pEnt->setLayer(matLayerW.c_str());
                    else if (pEnt->isKindOf(AcDbCircle::desc()))
                        pEnt->setLayer(TrinityLayerManager::LAYER_BOLT);
                    else
                        pEnt->setLayer(TrinityLayerManager::LAYER_TAG);
                    pEnt->close();
                }
            }
            delete pIter;
        }
        pMs2->close();
    }

    return cleanDb;
}

// ============================================
// ПОСТРОЕНИЕ КОНСТРУКЦИИ / ПРОЕКТА
// ============================================
// Рекурсивно собираем файлы детей и вставляем их XREF-ами
// во временную базу, затем wblock в чистую.
// ============================================
AcDbDatabase* TrinityBuildEngine::buildDwg(const TrinityNeuron& neuron, int depth) {
    if (neuron.type == "detail") return buildDetail(neuron);

    std::unique_ptr<AcDbDatabase> tempDb(TrinityFileManager::createEmptyDwg());
    if (!tempDb) return nullptr;

    AcDbObjectIdArray ids;
    const auto children = m_core.loadChildren(neuron.id);

    for (const auto& syn : children) {
        const std::string childPath = ensureFileExists(syn.childCode, depth + 1);
        if (childPath.empty()) {
            trinityLog(L"[BuildEngine] Cannot prepare child: %s",
                       toWide(syn.childCode).c_str());
            continue;
        }

        const AcDbObjectId childId = m_files.attachXref(
            childPath, syn.childCode, syn.position.toAcGe(),
            syn.rotation, tempDb.get());

        if (childId != AcDbObjectId::kNull) ids.append(childId);
    }

    if (ids.isEmpty()) return nullptr;
    return wblockOrDeleteTemp(tempDb.release(), ids);
}

// ============================================
// ОБЕСПЕЧИТЬ СУЩЕСТВОВАНИЕ ФАЙЛА (без вставки XREF)
// ============================================
std::string TrinityBuildEngine::ensureFileExists(const std::string& code, int depth) {
    if (depth > MAX_DEPTH) {
        trinityLog(L"[BuildEngine] MAX DEPTH for %s", toWide(code).c_str());
        return {};
    }

    TrinityNeuronPtr pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return {};

    const std::string subdir   = subdirOf(m_files, *pNeuron);
    const std::string filePath = m_files.getFilePath(pNeuron->code, subdir);
    if (m_files.fileExists(pNeuron->code, subdir)) return filePath;

    std::unique_ptr<AcDbDatabase> db(
        pNeuron->type == "detail" ? buildDetail(*pNeuron)
                                  : buildDwg(*pNeuron, depth));
    if (!db) return {};

    if (!m_files.saveDwg(db.get(), filePath)) return {};
    Sleep(200);
    return filePath;
}

// ============================================
// ОБРАБОТКА ВСЕХ PENDING-ПРОЕКТОВ
// ============================================
int TrinityBuildEngine::processAllProjects(AcDbDatabase* targetDb) {
    const auto projects = m_core.loadPendingProjects();
    if (projects.empty()) return 0;

    for (const auto& proj : projects) {
        // Полная пересборка: удаляем старые файлы дерева проекта
        deleteProjectFiles(proj.code);

        const std::string path = ensureFileExists(proj.code, 0);
        if (path.empty()) {
            trinityLog(L"[BuildEngine] Failed to create project: %s",
                       toWide(proj.code).c_str());
            continue;
        }

        m_core.markNeuronDone(proj.id);
        trinityLog(L"[BuildEngine] Project done: %s", toWide(proj.code).c_str());

        // Вставить проект в текущий документ как XREF
        if (targetDb) {
            m_files.attachXref(path, proj.code, AcGePoint3d::kOrigin,
                               TrinityRotationCompound{}, targetDb);
        }
    }
    return static_cast<int>(projects.size());
}

// ============================================
// РЕКУРСИВНОЕ УДАЛЕНИЕ ФАЙЛОВ ПРОЕКТА
// ============================================
void TrinityBuildEngine::deleteProjectFiles(const std::string& code) {
    TrinityNeuronPtr pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return;

    const std::string subdir = subdirOf(m_files, *pNeuron);
    if (m_files.removeFile(pNeuron->code, subdir)) {
        trinityLog(L"[BuildEngine] Deleted file: %s.dwg",
                   toWide(pNeuron->code).c_str());
    }

    if (pNeuron->type != "detail") {
        for (const auto& syn : m_core.loadChildren(pNeuron->id)) {
            deleteProjectFiles(syn.childCode);
        }
    }
}
