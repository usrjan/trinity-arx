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

bool TrinityFileManager::deleteDwgWithLocks(const std::string& code, const std::string& subdir) const {
    // AutoCAD при открытии/присоединении DWG создаёт lock-файлы <имя>.dwl и <имя>.dwl2.
    // При работе через side-database (readDwgFile/attachXref) они могут оставаться
    // в папках details/assemblies/projects — удаляем их вместе с основным файлом.
    const std::string base = getFilePath(code, subdir);   // ...\CODE.dwg

    bool mainOk = true;
    if (_access(base.c_str(), 0) == 0) {
        mainOk = (_unlink(base.c_str()) == 0);
    }

    // Побочные lock-файлы: "<code>.dwg" -> "<code>.dwl" / "<code>.dwl2"
    std::string dwl  = base.substr(0, base.size() - 4) + ".dwl";   // .dwg -> .dwl
    std::string dwl2 = base.substr(0, base.size() - 4) + ".dwl2";  // .dwg -> .dwl2

    for (const std::string& lock : { dwl, dwl2 }) {
        if (_access(lock.c_str(), 0) != 0) continue;       // нет файла — нечего удалять
        // Lock-файлы иногда помечены как read-only или удерживаются ещё мгновение —
        // снимаем атрибут и повторяем попытку после короткой паузы.
        if (_unlink(lock.c_str()) != 0) {
            _chmod(lock.c_str(), _S_IWRITE);
            Sleep(100);
            _unlink(lock.c_str());
        }
    }

    return mainOk;
}

AcDbDatabase* TrinityFileManager::createEmptyDwg() {
    return new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
}

bool TrinityFileManager::saveDwg(AcDbDatabase* db, const std::string& path) {
    wchar_t pathW[512];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, pathW, 512);

    Acad::ErrorStatus es = db->saveAs(pathW);
    if (es != Acad::eOk) {
        acutPrintf(_T("\n[FileManager] Save failed: %s (error %d)\n"), pathW, es);
        return false;
    }

    // NB: saveAs() привязывает базу к новому файлу — AutoCAD создаёт lock-файлы
    // <имя>.dwl / <имя>.dwl2 и держит их, пока база открыта. Без явного закрытия
    // они оставались в details/assemblies даже после delete db и удаления DWG.
    // Закрытие базы (delete) снимает блокировки и удаляет *.dwl/*.dwl2.
    es = db->close();
    if (es != Acad::eOk) {
        acutPrintf(_T("\n[FileManager] close after save failed (error %d), trying closeAll()\n"), es);
        if (db->closeAll() != Acad::eOk) {
            acutPrintf(_T("\n[FileManager] closeAll also failed — lock files may remain: %s\n"), pathW);
        }
    }
    return true;
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

    if (!targetDb) {
        acutPrintf(_T("\n[FileManager] attachXref: targetDb is null\n"));
        return AcDbObjectId::kNull;
    }

    AcDbObjectId blockId = AcDbObjectId::kNull;
    bool wasInserted = false;

    // Шаг 1: Проверяем, есть ли блок уже в таблице
    AcDbBlockTable* pBlockTable = nullptr;
    Acad::ErrorStatus es = targetDb->getSymbolTable(pBlockTable, AcDb::kForRead);
    if (es != Acad::eOk || !pBlockTable) {
        // getSymbolTable может вернуть eOk с nullptr при нехватке памяти (OOM) —
        // проверяем указатель до разыменования, иначе будет падение
        acutPrintf(_T("\n[FileManager] attachXref: Cannot open BlockTable (error %d)\n"), es);
        return AcDbObjectId::kNull;
    }
    if (pBlockTable->has(nameW)) {
        // Блок существует — проверяем, валиден ли его путь
        AcDbBlockTableRecord* pBlockRec = nullptr;
        es = pBlockTable->getAt(nameW, pBlockRec, AcDb::kForRead);
        if (es == Acad::eOk && pBlockRec) {
            // Если это XREF — проверяем, существует ли файл
            if (pBlockRec->isFromExternalReference()) {
                // Получаем путь к внешнему файлу через AcString
                AcString xrefPath;
                Acad::ErrorStatus pathEs = pBlockRec->pathName(xrefPath);

                bool fileExists = false;
                if (pathEs == Acad::eOk) {
                    fileExists = (_waccess(xrefPath.kwszPtr(), 0) == 0);
                }

                if (!fileExists) {
                    // Файл удалён — нужно удалить старый блок и вставить заново
                    pBlockRec->close();
                    pBlockTable->upgradeOpen();

                    // Удаляем старый блок из таблицы
                    AcDbObjectId oldBlockId;
                    pBlockTable->getAt(nameW, oldBlockId);

                    // Открываем для записи и удаляем
                    AcDbBlockTableRecord* pOldRec = nullptr;
                    es = pBlockTable->getAt(nameW, pOldRec, AcDb::kForWrite);
                    if (es == Acad::eOk && pOldRec) {
                        pOldRec->erase();
                        pOldRec->close();
                    }
                    pBlockTable->close();

                    // Теперь блока нет — будем вставлять заново
                    blockId = AcDbObjectId::kNull;
                    wasInserted = false;
                } else {
                    pBlockRec->close();
                    pBlockTable->getAt(nameW, blockId);
                    pBlockTable->close();
                    wasInserted = true;
                }
            } else {
                pBlockRec->close();
                pBlockTable->getAt(nameW, blockId);
                pBlockTable->close();
                wasInserted = true;
            }
        } else {
            if (pBlockRec) pBlockRec->close();
            pBlockTable->close();
        }
    } else {
        pBlockTable->close();
    }

    // Шаг 2: Если блока нет — читаем файл и вставляем
    if (blockId == AcDbObjectId::kNull) {
        // NB: ObjectARX переопределяет operator new для AcDbDatabase (AcHeapOperators),
        // который не поддерживает placement-форму std::nothrow (ошибка C2661),
        // поэтому используется обычный new.
        // Флаг noVersionCheck=true — чтобы DWG, сохранённый другой версией/
        // конструктором базы, читался без ошибки версии.
        // NB: readDwgFile НЕ закрывает базу при ошибке — её всё равно нужно
        // удалить, иначе утечка. Поэтому create/read/insert и очистка разделены.
        AcDbDatabase* pXrefDb = new AcDbDatabase(false, true);
        if (!pXrefDb) {
            acutPrintf(_T("\n[FileManager] attachXref: out of memory\n"));
            return AcDbObjectId::kNull;
        }

        es = pXrefDb->readDwgFile(pathW);
        if (es != Acad::eOk) {
            acutPrintf(_T("\n[FileManager] readDwgFile failed: %s (error %d)\n"), pathW, es);
            delete pXrefDb;   // база не закрыта readDwgFile — удаляем сами
            return AcDbObjectId::kNull;
        }

        // NB: AcDbDatabase::insert() ЗАКРЫВАЕТ переданную базу самостоятельно
        // (независимо от кода возврата). Повторный delete вызывал
        // Access Violation (double-delete / use-after-free в куче ObjectARX).
        es = targetDb->insert(blockId, nameW, pXrefDb, true);
        wasInserted = (es == Acad::eOk);

        // Шаг 3: ОБРАБОТКА РЕЗУЛЬТАТА
        if (es == Acad::eDuplicateKey) {
            // Блок уже есть (гонка или скрытый блок) — получаем его ID
            AcDbBlockTable* pBt = nullptr;
            Acad::ErrorStatus btEs = targetDb->getSymbolTable(pBt, AcDb::kForRead);
            if (btEs == Acad::eOk && pBt) {
                if (pBt->has(nameW)) {
                    pBt->getAt(nameW, blockId);
                }
                pBt->close();
                wasInserted = true;
            } else {
                acutPrintf(_T("\n[FileManager] Cannot open BlockTable on duplicate (error %d)\n"), btEs);
                return AcDbObjectId::kNull;
            }
        }
        else if (es != Acad::eOk) {
            acutPrintf(_T("\n[FileManager] Insert failed (error %d)\n"), es);
            return AcDbObjectId::kNull;
        }
    }

    if (blockId == AcDbObjectId::kNull) {
        acutPrintf(_T("\n[FileManager] Block not found: %s\n"), nameW);
        return AcDbObjectId::kNull;
    }

    // Шаг 4: Добавляем BlockReference в Model Space
    AcDbBlockTable* pBt = nullptr;
    es = targetDb->getSymbolTable(pBt, AcDb::kForRead);
    if (es != Acad::eOk || !pBt) {
        acutPrintf(_T("\n[FileManager] Cannot open BlockTable (error %d)\n"), es);
        return AcDbObjectId::kNull;
    }
    
    AcDbBlockTableRecord* pMs = nullptr;
    es = pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBt->close();
    
    if (es != Acad::eOk || !pMs) {
        acutPrintf(_T("\n[FileManager] Cannot open ModelSpace (error %d)\n"), es);
        if (pMs) pMs->close();
        return AcDbObjectId::kNull;
    }

    AcDbBlockReference* pRef = new AcDbBlockReference(pos, blockId);
    if (!pRef) {
        pMs->close();
        return AcDbObjectId::kNull;
    }

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
    es = pMs->appendAcDbEntity(refId, pRef);
    
    // Всегда закрываем pRef, независимо от результата
    pRef->close();
    pMs->close();

    if (es != Acad::eOk) {
        acutPrintf(_T("\n[FileManager] Failed to append BlockReference (error %d)\n"), es);
        return AcDbObjectId::kNull;
    }

    return refId;
}