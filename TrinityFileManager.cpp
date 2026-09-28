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
    // ВАЖНО (фикс Access Violation): раньше путь писался в фиксированный
    // буфер wchar_t[512]. MultiByteToWideChar при переполнении НЕ пишет
    // ничего и оставляет буфер НЕЗАПИСАННЫМ (мусор стека), а db->saveAs()
    // затем читал этот мусор как строку — чтение из невалидной памяти
    // давало "Unhandled Access Violation Reading 0x..." внутри acad.exe.
    // Теперь размер вычисляется точно, конвертация проверяется на успех.
    if (!db) return false;

    std::wstring w = utf8ToWideLocal(path);
    if (w.empty()) {
        acutPrintf(_T("\n[FileManager] Bad path encoding: %hs\n"), path.c_str());
        return false;
    }

    Acad::ErrorStatus es = db->saveAs(w.c_str());
    if (es == Acad::eOk) {
        //acutPrintf(_T("\n[FileManager] Saved: %ls\n"), pathW);
        return true;
    }
    acutPrintf(_T("\n[FileManager] Save failed: %ls (error %d)\n"), w.c_str(), es);
    return false;
}

AcDbObjectId TrinityFileManager::attachXref(
    const std::string& path,
    const std::string& name,
    const AcGePoint3d& pos,
    const TrinityRotationCompound& rot,
    AcDbDatabase* targetDb)
{
    // ВАЖНО (фикс Access Violation): вместо фиксированных буферов
    // wchar_t[512]/wchar_t[256] — точные std::wstring. При переполнении
    // буфера MultiByteToWideChar не пишет ничего, и дальнейшее чтение
    // незаписанного стекового мусора как строки давало AV внутри acad.exe.
    if (!targetDb) return AcDbObjectId::kNull;

    std::wstring pathW = utf8ToWideLocal(path);
    std::wstring nameW = utf8ToWideLocal(name);
    if (pathW.empty() || nameW.empty()) {
        acutPrintf(_T("\n[FileManager] Bad path/name encoding: %hs\n"), path.c_str());
        return AcDbObjectId::kNull;
    }

    if (_waccess(pathW.c_str(), 0) != 0) {
        acutPrintf(_T("\n[FileManager] File not found: %ls\n"), pathW.c_str());
        return AcDbObjectId::kNull;
    }

    AcDbObjectId blockId = AcDbObjectId::kNull;
    bool wasInserted = false;

    // Шаг 1: Проверяем, есть ли блок уже в таблице
    AcDbBlockTable* pBlockTable = nullptr;
    Acad::ErrorStatus es = targetDb->getSymbolTable(pBlockTable, AcDb::kForRead);
    if (es == Acad::eOk) {
        // has()/getAt() требуют const wchar_t* — раньше передавались
        // неинициализированные при переполнении буферы, теперь .c_str().
        if (pBlockTable->has(nameW.c_str())) {
            // Блок существует — проверяем, валиден ли его путь
            AcDbBlockTableRecord* pBlockRec = nullptr;
            es = pBlockTable->getAt(nameW.c_str(), pBlockRec, AcDb::kForRead);
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
                        
                        // Открываем для записи и удаляем
                        AcDbBlockTableRecord* pOldRec = nullptr;
                        es = pBlockTable->getAt(nameW.c_str(), pOldRec, AcDb::kForWrite);
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
                        pBlockTable->getAt(nameW.c_str(), blockId);
                        pBlockTable->close();
                        wasInserted = true;
                    }
                } else {
                    pBlockRec->close();
                    pBlockTable->getAt(nameW.c_str(), blockId);
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
        es = pXrefDb->readDwgFile(pathW.c_str());

        if (es == Acad::eOk) {
            es = targetDb->insert(blockId, nameW.c_str(), pXrefDb, true);
            wasInserted = (es == Acad::eOk);
        }
        delete pXrefDb;   // insert(...,true) клонирует базу-источник — удалять свою копию безопасно

        // Шаг 3: ОБРАБОТКА РЕЗУЛЬТАТА
        if (es == Acad::eDuplicateKey) {
            // Блок уже есть (гонка или скрытый блок) — получаем его ID.
            // ВАЖНО (фикс Access Violation): раньше результат
            // getSymbolTable использовался без проверки — при ошибке
            // pBt оставался nullptr и pBt->has(...) читал нулевой адрес.
            AcDbBlockTable* pBt = nullptr;
            Acad::ErrorStatus esBt = targetDb->getSymbolTable(pBt, AcDb::kForRead);
            if (esBt == Acad::eOk && pBt) {
                if (pBt->has(nameW.c_str())) {
                    pBt->getAt(nameW.c_str(), blockId);
                }
                pBt->close();
                wasInserted = true;
            }
        }
        else if (es != Acad::eOk) {
            acutPrintf(_T("\n[FileManager] Insert failed (error %d)\n"), es);
            return AcDbObjectId::kNull;
        }
    }

    if (blockId == AcDbObjectId::kNull) {
        acutPrintf(_T("\n[FileManager] Block not found: %ls\n"), nameW.c_str());
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