// TrinityFileManager.cpp
#include "StdAfx.h"
#include "TrinityFileManager.h"
#include <direct.h>
#include <io.h>

TrinityFileManager::TrinityFileManager(const std::string& basePath) : m_basePath(basePath) {
    _mkdir((m_basePath + "\\details").c_str());
    _mkdir((m_basePath + "\\assemblies").c_str());
    _mkdir((m_basePath + "\\projects").c_str());
}

bool TrinityFileManager::fileExists(const std::string& code, const std::string& subdir) const {
    std::string path = getFilePath(code, subdir);
    return _access(path.c_str(), 0) == 0;
}

std::string TrinityFileManager::getFilePath(const std::string& code, const std::string& subdir) const {
    return m_basePath + "\\" + subdir + "\\" + code + ".dwg";
}

AcDbDatabase* TrinityFileManager::createEmptyDwg() {
    return new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
}

bool TrinityFileManager::saveDwg(AcDbDatabase* db, const std::string& path) {
    wchar_t pathW[512];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, pathW, 512);
    Acad::ErrorStatus es = db->saveAs(pathW);
    if (es == Acad::eOk) { acutPrintf(_T("\n[FileManager] Saved: %s\n"), pathW); return true; }
    acutPrintf(_T("\n[FileManager] Save failed: %s (error %d)\n"), pathW, es);
    return false;
}

AcDbObjectId TrinityFileManager::attachXref(const std::string& path, const std::string& name,
                                              const AcGePoint3d& pos, const TrinityRotationCompound& rot,
                                              AcDbDatabase* targetDb) {
    wchar_t pathW[512], nameW[256];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, pathW, 512);
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nameW, 256);
    if (_waccess(pathW, 0) != 0) { acutPrintf(_T("\n[FileManager] File not found: %s\n"), pathW); return AcDbObjectId::kNull; }

    AcDbBlockTable* pBlockTable = nullptr;
    if (targetDb->getSymbolTable(pBlockTable, AcDb::kForRead) != Acad::eOk) return AcDbObjectId::kNull;
    AcDbObjectId blockId = AcDbObjectId::kNull;
    bool blockExists = pBlockTable->has(nameW);
    if (blockExists) pBlockTable->getAt(nameW, blockId);
    pBlockTable->close();

    if (!blockExists) {
        AcDbDatabase* pXrefDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
        if (pXrefDb->readDwgFile(pathW) == Acad::eOk)
            targetDb->insert(blockId, nameW, pXrefDb, true);
        delete pXrefDb;
    }
    if (blockId == AcDbObjectId::kNull) return AcDbObjectId::kNull;

    AcDbBlockTable* pBt = nullptr; targetDb->getSymbolTable(pBt, AcDb::kForRead);
    AcDbBlockTableRecord* pMs = nullptr; pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBt->close();

    AcDbBlockReference* pRef = new AcDbBlockReference(pos, blockId);
    if (rot.count > 0) {
        AcGeMatrix3d mat; mat.setToIdentity();
        for (int i = 0; i < rot.count; i++) {
            const auto& r = rot.rotations[i];
            if (r.angle != 0) {
                AcGeVector3d axis(r.x, r.y, r.z);
                if (!axis.isZeroLength()) {
                    axis.normalize();
                    AcGeMatrix3d rotMat;
                    rotMat.setToRotation(r.angle * M_PI / 180.0, axis, pos);
                    mat = mat * rotMat;
                }
            }
        }
        pRef->transformBy(mat);
    }
    AcDbObjectId refId;
    pMs->appendAcDbEntity(refId, pRef);
    pRef->close(); pMs->close();
    return refId;
}