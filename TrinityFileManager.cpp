// TrinityFileManager.cpp
#include "StdAfx.h"
#include "TrinityFileManager.h"
#include <direct.h>
#include <io.h>

// Утилита: преобразование UTF-8 std::string -> std::wstring
static std::wstring utf8ToWideLocal(const std::string& s) {
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], need);
    w.resize(static_cast<size_t>(need) - 1); // убрать терминальный ноль
    return w;
}

// Рекурсивное создание директории по широкому пути.
// Идём по разделителям ('\' и '/') и создаём каждую промежуточную папку.
// _wmkdir возвращает 0 при успехе; errno==EEXIST означает «папка уже есть» —
// это не ошибка. Остальные сбои логируются и пробрасываются как false.
bool TrinityFileManager::createDirectoryRecursiveW(const wchar_t* wpath) {
    if (!wpath || !*wpath) return false;

    std::wstring path(wpath);
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == L'/') path[i] = L'\\';

    size_t start = 0;
    bool isUnc = false;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // UNC-путь \\server\share\...: сервер/шару не создаём
        size_t p1 = path.find(L'\\', 2);
        if (p1 == std::wstring::npos) return true;      // только сервер — ничего создавать не нужно
        size_t p2 = path.find(L'\\', p1 + 1);
        if (p2 == std::wstring::npos) return true;      // сервер+шара — тоже существующая реальность
        start = p2 + 1;
        isUnc = true;
    } else if (path.size() >= 2 && path[1] == L':') {
        start = 2; // диск C:\...
    }

    std::wstring cur;
    size_t i = 0;
    while (i < path.size()) {
        size_t sep = path.find(L'\\', i);
        if (sep == std::wstring::npos) sep = path.size();
        if (sep > i) {
            cur = path.substr(0, sep);
            // Не пытаемся создать корень ("C:\" или "\\srv\sh\")
            const size_t rootLen = isUnc ? start : (start == 2 ? 3 : 0);
            if (cur.size() > rootLen) {
                if (_waccess(cur.c_str(), 0) != 0) {
                    if (_wmkdir(cur.c_str()) != 0 && errno != EEXIST) {
                        acutPrintf(_T("\n[FileManager] mkdir failed: %ls (errno=%d)\n"),
                                   cur.c_str(), errno);
                        return false;
                    }
                }
            }
        }
        i = sep + 1;
    }
    return true;
}

bool TrinityFileManager::createDirectoryRecursiveA(const std::string& pathUtf8) {
    std::wstring w = utf8ToWideLocal(pathUtf8);
    if (w.empty()) {
        acutPrintf(_T("\n[FileManager] Bad path encoding: %hs\n"), pathUtf8.c_str());
        return false;
    }
    return createDirectoryRecursiveW(w.c_str());
}

// Извлекает родительную директорию из полного пути к файлу (UTF-8).
std::string TrinityFileManager::parentDir(const std::string& filePath) {
    size_t pos = filePath.find_last_of("\\/");
    if (pos == std::string::npos) return std::string();
    return filePath.substr(0, pos);
}

TrinityFileManager::TrinityFileManager(const std::string& basePath)
    : m_basePath(basePath) {
    // Рекурсивно: если m_basePath ещё нет вместе с промежуточными папками —
    // они будут созданы. Unicode-функции: кириллические пути сохраняются.
    createDirectoryRecursiveA(m_basePath + "\\details");
    createDirectoryRecursiveA(m_basePath + "\\assemblies");
    createDirectoryRecursiveA(m_basePath + "\\projects");
}

bool TrinityFileManager::fileExists(const std::string& code, const std::string& subdir) const {
    std::string path = getFilePath(code, subdir);
    std::wstring w = utf8ToWideLocal(path);
    return !w.empty() && _waccess(w.c_str(), 0) == 0;
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
        //acutPrintf(_T("\n[FileManager] Saved: %ls\n"), pathW);
        return true;
    }
    acutPrintf(_T("\n[FileManager] Save failed: %ls (error %d)\n"), pathW, es);
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
        acutPrintf(_T("\n[FileManager] File not found: %ls\n"), pathW);
        return AcDbObjectId::kNull;
    }

    AcDbObjectId blockId = AcDbObjectId::kNull;
    bool wasInserted = false;

    // Шаг 1: Проверяем, есть ли блок уже в таблице
    AcDbBlockTable* pBlockTable = nullptr;
    Acad::ErrorStatus es = targetDb->getSymbolTable(pBlockTable, AcDb::kForRead);
    if (es == Acad::eOk) {
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
    }

    // Шаг 2: Если блока нет — читаем файл и вставляем
    if (blockId == AcDbObjectId::kNull) {
        AcDbDatabase* pXrefDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
        es = pXrefDb->readDwgFile(pathW);

        if (es == Acad::eOk) {
            es = targetDb->insert(blockId, nameW, pXrefDb, true);
            wasInserted = (es == Acad::eOk);
        }
        delete pXrefDb;

        // Шаг 3: ОБРАБОТКА РЕЗУЛЬТАТА
        if (es == Acad::eDuplicateKey) {
            // Блок уже есть (гонка или скрытый блок) — получаем его ID
            AcDbBlockTable* pBt = nullptr;
            targetDb->getSymbolTable(pBt, AcDb::kForRead);
            if (pBt->has(nameW)) {
                pBt->getAt(nameW, blockId);
            }
            pBt->close();
            wasInserted = true;
        }
        else if (es != Acad::eOk) {
            acutPrintf(_T("\n[FileManager] Insert failed (error %d)\n"), es);
            return AcDbObjectId::kNull;
        }
    }

    if (blockId == AcDbObjectId::kNull) {
        acutPrintf(_T("\n[FileManager] Block not found: %ls\n"), nameW);
        return AcDbObjectId::kNull;
    }

    // Шаг 4: Добавляем BlockReference в Model Space
    AcDbBlockTable* pBt = nullptr;
    es = targetDb->getSymbolTable(pBt, AcDb::kForRead);
    if (es != Acad::eOk) {
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