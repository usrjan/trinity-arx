// TrinityFileManager.cpp
#include "StdAfx.h"
#include "TrinityFileManager.h"
#include "TrinityGeometryBuilder.h"
#include <direct.h>
#include <io.h>

TrinityFileManager::TrinityFileManager(const std::string& basePath)
    : m_basePath(basePath) {
    _mkdir(m_basePath.c_str());
    _mkdir((m_basePath + "\\details").c_str());
    _mkdir((m_basePath + "\\assemblies").c_str());
    _mkdir((m_basePath + "\\projects").c_str());
}

std::string TrinityFileManager::subdirForType(const std::string& type) {
    if (type == "detail")                                  return "details";
    if (type == "assembly" || type == "construction")      return "assemblies";
    return "projects";
}

bool TrinityFileManager::fileExists(const std::string& code,
                                    const std::string& subdir) const {
    const std::wstring pathW = toWide(getFilePath(code, subdir));
    return _waccess(pathW.c_str(), 0) == 0;
}

std::string TrinityFileManager::getFilePath(const std::string& code,
                                            const std::string& subdir) const {
    return m_basePath + "\\" + subdir + "\\" + code + ".dwg";
}

bool TrinityFileManager::removeFile(const std::string& code,
                                    const std::string& subdir) const {
    const std::wstring pathW = toWide(getFilePath(code, subdir));
    return _wunlink(pathW.c_str()) == 0;
}

AcDbDatabase* TrinityFileManager::createEmptyDwg() {
    return new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
}

bool TrinityFileManager::saveDwg(AcDbDatabase* db, const std::string& path) {
    if (!db) return false;
    const std::wstring pathW = toWide(path);

    if (db->saveAs(pathW.c_str()) == Acad::eOk) return true;
    trinityLog(L"[FileManager] Save failed: %s", pathW.c_str());
    return false;
}

// ============================================
// Проверка существующего блока XREF: если файл на диске
// пропал — блок удаляется, чтобы вставить заново.
// ============================================
static bool blockNeedsReinsert(AcDbBlockTableRecord* pRec) {
    if (!pRec->isFromExternalReference()) return false;
    AcString xrefPath;
    if (pRec->pathName(xrefPath) != Acad::eOk) return false;
    return _waccess(xrefPath.kwszPtr(), 0) != 0;
}

AcDbObjectId TrinityFileManager::attachXref(const std::string& path,
                                            const std::string& name,
                                            const AcGePoint3d& pos,
                                            const TrinityRotationCompound& rot,
                                            AcDbDatabase* targetDb) {
    if (!targetDb) return AcDbObjectId::kNull;

    const std::wstring pathW = toWide(path);
    const std::wstring nameW = toWide(name);

    if (_waccess(pathW.c_str(), 0) != 0) {
        trinityLog(L"[FileManager] File not found: %s", pathW.c_str());
        return AcDbObjectId::kNull;
    }

    // ---- Шаг 1: существует ли блок? ---------------------------------------
    AcDbObjectId blockId = AcDbObjectId::kNull;
    {
        AcDbBlockTable* pBT = nullptr;
        if (targetDb->getSymbolTable(pBT, AcDb::kForRead) == Acad::eOk) {
            if (pBT->has(nameW.c_str())) {
                AcDbBlockTableRecord* pRec = nullptr;
                if (pBT->getAt(nameW.c_str(), pRec, AcDb::kForRead) == Acad::eOk && pRec) {
                    if (blockNeedsReinsert(pRec)) {
                        pRec->erase();               // устаревший XREF-блок
                    } else {
                        pBT->getAt(nameW.c_str(), blockId);
                    }
                    pRec->close();
                }
            }
            pBT->close();
        }
    }

    // ---- Шаг 2: вставить блок из файла ------------------------------------
    if (blockId == AcDbObjectId::kNull) {
        std::unique_ptr<AcDbDatabase> pXrefDb(new AcDbDatabase(Adesk::kTrue, Adesk::kTrue));
        Acad::ErrorStatus es = pXrefDb->readDwgFile(pathW.c_str());
        if (es == Acad::eOk) {
            es = targetDb->insert(blockId, nameW.c_str(), pXrefDb.get(), true);
        }
        if (es == Acad::eDuplicateKey) {
            // гонка: блок появился параллельно — берём существующий
            AcDbBlockTable* pBT = nullptr;
            if (targetDb->getSymbolTable(pBT, AcDb::kForRead) == Acad::eOk) {
                if (pBT->has(nameW.c_str())) pBT->getAt(nameW.c_str(), blockId);
                pBT->close();
            }
        } else if (es != Acad::eOk) {
            trinityLog(L"[FileManager] Insert failed for %s (error %d)",
                       nameW.c_str(), static_cast<int>(es));
            return AcDbObjectId::kNull;
        }
    }

    if (blockId == AcDbObjectId::kNull) {
        trinityLog(L"[FileManager] Block not found: %s", nameW.c_str());
        return AcDbObjectId::kNull;
    }

    // ---- Шаг 3: BlockReference в Model Space ------------------------------
    AcDbBlockTable* pBT = nullptr;
    if (targetDb->getSymbolTable(pBT, AcDb::kForRead) != Acad::eOk) {
        trinityLog(L"[FileManager] Cannot open BlockTable");
        return AcDbObjectId::kNull;
    }
    AcDbBlockTableRecord* pMs = nullptr;
    const Acad::ErrorStatus esMs = pBT->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBT->close();
    if (esMs != Acad::eOk || !pMs) {
        trinityLog(L"[FileManager] Cannot open ModelSpace");
        return AcDbObjectId::kNull;
    }

    AcDbBlockReference* pRef = new AcDbBlockReference(pos, blockId);
    const AcGeMatrix3d mat =
        TrinityGeometryBuilder::rotationMatrix(rot, pos);
    if (rot.count > 0) pRef->transformBy(mat);

    AcDbObjectId refId;
    const Acad::ErrorStatus esApp = pMs->appendAcDbEntity(refId, pRef);
    pMs->close();

    if (esApp != Acad::eOk) {
        delete pRef;
        trinityLog(L"[FileManager] Failed to append BlockReference (error %d)",
                   static_cast<int>(esApp));
        return AcDbObjectId::kNull;
    }
    return refId;
}
