// TrinityFileManager.cpp
#include "StdAfx.h"
#include "TrinityFileManager.h"
#include <direct.h>
#include <io.h>

TrinityFileManager::TrinityFileManager(const std::string& basePath)
    : m_basePath(basePath) {
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
    if (es == Acad::eOk) {
        acutPrintf(_T("\n[FileManager] Saved: %s\n"), pathW);
        return true;
    }
    acutPrintf(_T("\n[FileManager] Save failed: %s (error %d)\n"), pathW, es);
    return false;
}

AcDbObjectId TrinityFileManager::attachXref(
    const std::string& path,
    const std::string& name,
    const AcGePoint3d& pos,
    const TrinityRotationCompound& rot,
    AcDbDatabase* targetDb)
{
    wchar_t pathW[512], nameW[256];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, pathW, 512);
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nameW, 256);

    if (_waccess(pathW, 0) != 0) {
        acutPrintf(_T("\n[FileManager] File not found: %s\n"), pathW);
        return AcDbObjectId::kNull;
    }

    AcDbObjectId blockId = AcDbObjectId::kNull;

    // Шаг 1: есть ли блок уже?
    AcDbBlockTable* pBlockTable = nullptr;
    Acad::ErrorStatus es = targetDb->getSymbolTable(pBlockTable, AcDb::kForRead);
    if (es == Acad::eOk) {
        if (pBlockTable->has(nameW)) {
            pBlockTable->getAt(nameW, blockId);
        }
        pBlockTable->close();
    }

    // Шаг 2: если блока нет — читаем файл и вставляем
    if (blockId == AcDbObjectId::kNull) {
        AcDbDatabase* pXrefDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
        es = pXrefDb->readDwgFile(pathW);

        if (es == Acad::eOk) {
            es = targetDb->insert(blockId, nameW, pXrefDb, true);
        }
        delete pXrefDb;

        // Шаг 3: ОБРАБОТКА РЕЗУЛЬТАТА
        if (es == Acad::eDuplicateKey) {
            // Блок уже есть (гонка или скрытый блок) — получаем его ID
            AcDbBlockTable* pBt = nullptr;
            targetDb->getSymbolTable(pBt, AcDb::kForRead);
            if (pBt->has(nameW)) {
                pBt->getAt(nameW, blockId);
                acutPrintf(_T("\n[FileManager] Block exists (dup), using it\n"));
            }
            pBt->close();
        }
        else if (es != Acad::eOk) {
            // Любая другая ошибка — выходим
            acutPrintf(_T("\n[FileManager] Insert failed (error %d)\n"), es);
            return AcDbObjectId::kNull;
        }
        else {
            acutPrintf(_T("\n[FileManager] XREF inserted: %s\n"), nameW);
        }
    }

    if (blockId == AcDbObjectId::kNull) {
        acutPrintf(_T("\n[FileManager] Block not found: %s\n"), nameW);
        return AcDbObjectId::kNull;
    }

    // Шаг 4: добавляем BlockReference в Model Space
    AcDbBlockTable* pBt = nullptr;
    targetDb->getSymbolTable(pBt, AcDb::kForRead);
    AcDbBlockTableRecord* pMs = nullptr;
    pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBt->close();

    AcDbBlockReference* pRef = new AcDbBlockReference(pos, blockId);

    if (rot.count > 0) {
        AcGeMatrix3d mat;
        mat.setToIdentity();
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
    pRef->close();
    pMs->close();

    acutPrintf(_T("\n[FileManager] XREF placed: %s\n"), nameW);
    return refId;
}