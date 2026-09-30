// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"
#include <cstring>   // strlen (парсинг holes)
// Примечание: <io.h> уже включён через StdAfx.h — дубль убран.

// ============================================================
// ПРИМЕЧАНИЕ: вставка готового DWG проекта в текущий чертёж НЕ делается.
// Метод ensureExists был удалён как мёртвый код, реализованная вместо
// него insertProjectToTarget вызывала ошибку 320 (eWasOpenForWrite) при
// работе с активным документом и была убрана по решению заказчика:
// достаточно того, что процесс сборки создаёт файл проекта на диске.
// Единственный потребитель attachXref — buildDwg (вставка детей как
// XREF во ВРЕМЕННУЮ базу с последующим wblock).
// ============================================================

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
    // Фикс AV: точный размер вместо wchar_t[256] (при переполнении буфер
    // оставался неинициализированным — setLayer читал мусор стека).
    std::wstring layerW = utf8ToWide(layer);
    if (!layerW.empty()) solid->setLayer(layerW.c_str());

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

        // Парсим holes из JSON (ручной поиск подстрок — временное решение,
        // см. рекомендацию по nlohmann/json). Обходим массив поэлементно:
        // границы каждого объекта {"..."} находятся по '{' и '}'.
        // Исправлены две ошибки прежней версии:
        //   1) бесконечный цикл: при отсутствии "y" индекс сдвигался как
        //      npos + 1 (переполнение size_t -> 0) и поиск начинался заново;
        //   2) склейка значений между объектами ("y" искался по всему массиву,
        //      мог взять координату из следующего элемента или дать мусорный
        //      ноль вместо пропуска неполного элемента).
        size_t holesPos = detail.jsonData.find("\"holes\"");
        if (holesPos != std::string::npos) {
            size_t arrStart = detail.jsonData.find('[', holesPos);
            if (arrStart != std::string::npos) {
                size_t arrEnd = detail.jsonData.find(']', arrStart);
                if (arrEnd != std::string::npos && arrEnd > arrStart) {
                    std::string holesStr = detail.jsonData.substr(arrStart, arrEnd - arrStart + 1);

                    // Извлекает числовое значение по ключу внутри одного объекта;
                    // false — ключа или ':' нет.
                    auto findNumInObj = [](const std::string& obj, const char* key, double& out) -> bool {
                        size_t k = obj.find(key);
                        if (k == std::string::npos) return false;
                        size_t colon = obj.find(':', k + strlen(key));
                        if (colon == std::string::npos) return false;
                        out = atof(obj.c_str() + colon + 1);
                        return true;
                    };

                    size_t objStart = 0;
                    while ((objStart = holesStr.find('{', objStart)) != std::string::npos) {
                        size_t objEnd = holesStr.find('}', objStart);
                        if (objEnd == std::string::npos) break;          // обрывок JSON — стоп

                        std::string obj = holesStr.substr(objStart, objEnd - objStart + 1);
                        double x = 0, y = 0;
                        bool hasX = findNumInObj(obj, "\"x\"", x);
                        bool hasY = findNumInObj(obj, "\"y\"", y);
                        if (hasX && hasY) {
                            holePositions.push_back(AcGePoint3d(x, y, 0));
                        } else if (hasX || hasY) {
                            acutPrintf(_T("\n[BuildEngine] holes: incomplete element skipped: %hs\n"), obj.c_str());
                        }

                        objStart = objEnd + 1;                           // строгий прогресс
                    }
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

    if (es != Acad::eOk) {
        // При ошибке wblock не гарантирует корректного состояния выходного
        // указателя: он может остаться nullptr, а может указывать на
        // частично сконструированную базу. Удаление такого объекта приводит
        // к двойному free() / Access Violation — просто обнуляем указатель
        // и возвращаем ошибку. Если база всё же валидна (es == eOk, но
        // cleanDb == nullptr — формально невозможно, но защищаемся), её
        // тоже нельзя удалять вслепую.
        acutPrintf(_T("\n[BuildEngine] wblock failed: %d\n"), (int)es);
        cleanDb = nullptr;
        return nullptr;
    }
    if (!cleanDb) {
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

    std::wstring matLayerW = utf8ToWide(layer);  // фикс AV: точный размер

    for (pIter->start(); !pIter->done(); pIter->step()) {
        AcDbEntity* pEnt = nullptr;
        if (pIter->getEntity(pEnt, AcDb::kForWrite) == Acad::eOk && pEnt) {
            if (pEnt->isKindOf(AcDb3dSolid::desc())) {
                if (!matLayerW.empty())
                    pEnt->setLayer(matLayerW.c_str());  // солид → материал
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

    // Финальный отчёт
    /*
    wchar_t* wCode = utf2uni(detail.code.c_str());
    // Фикс: %s для wchar_t* — неопределённое поведение (ANSI-printf читает
    // wide-строку как char*, печатает мусор/краш). Только %ls.
    acutPrintf(_T("\n[BuildEngine] Detail built: %ls\n"), wCode);
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
    // Фикс: wchar_t* аргумент требует %ls, а не %s
    acutPrintf(_T("\n[BuildEngine] Building %ls: %d children (depth=%d)\n"),
        wCode, static_cast<int>(children.size()), depth);
    free(wCode);
    */

    for (auto& syn : children) {
        AcGePoint3d childPos = syn.position.toAcGe();

        /*
        wchar_t* wChild = utf2uni(syn.childCode.c_str());
        // Фикс: wchar_t* аргумент требует %ls, а не %s
        acutPrintf(_T("\n[BuildEngine] Child: %ls (depth=%d)\n"), wChild, depth);
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

    if (es != Acad::eOk) {
        // Не удаляем cleanDb вслепую: при ошибке wblock выходной указатель
        // может быть в неопределённом состоянии (частично сконструированная
        // база) — delete приведёт к двойному free() / Access Violation.
        acutPrintf(_T("\n[BuildEngine] buildDwg wblock failed: %d\n"), (int)es);
        return nullptr;
    }
    if (!cleanDb) {
        return nullptr;
    }

    return cleanDb;
}

// ============================================================
// ОБЕСПЕЧИТЬ СУЩЕСТВОВАНИЕ ФАЙЛА (без вставки XREF)
// ============================================================
// Используется внутри buildDwg для рекурсивной подготовки детей.
// Возвращает путь к файлу или пустую строку при ошибке.
//
// ОПТИМИЗАЦИЯ (кэш + защита от циклов):
//   1. Кэш m_builtCache — повторный запрос на уже собранный за текущую
//      сборку код возвращается мгновенно (O(1)), без loadNeuronByCode
//      (обращение к MySQL) и без fileExists (обращение к диску). Это
//      устраняет экспоненциальный рост нагрузки: компонент, входящий в
//      N конструкций, собирается ровно ОДИН раз за проход.
//   2. Множество m_activeCodes — если код уже находится в стеке вызовов
//      («строится прямо сейчас выше по рекурсии»), значит в графе связей
//      есть цикл (A -> B -> ... -> A). Немедленно возвращаем пустую
//      строку с диагностикой вместо бесконечной рекурсии.
//
// Инвариант: код добавляется в m_activeCodes сразу после проверки кэша и
// снимается ПОСЛЕ рекурсивного спуска на всех ветках выхода (см. шаги 2-4
// внутри функции) — гарантированно без утечек состояния.
// ============================================================
std::string TrinityBuildEngine::ensureFileExists(const std::string& code, int depth) {
    // Пустой код — заведомо ошибочный запрос (например, synapse с
    // битым child_code). Возвращаемся до работы с кэшем/БД.
    if (code.empty()) return "";

    // ------------------------------------------------------------
    // ШАГ 1. Быстрый путь: файл уже собран в рамках текущей сборки.
    // Обращение к хэш-карте O(1); при попадании не трогаем ни БД,
    // ни файловую систему.
    // ------------------------------------------------------------
    auto cached = m_builtCache.find(code);
    if (cached != m_builtCache.end()) {
        return cached->second;
    }

    // Защита от бесконечной рекурсии по глубине (страховка; основной
    // предохранитель от циклов — m_activeCodes ниже).
    if (depth > 20) {
        wchar_t* wCode = utf2uni(code.c_str());
        acutPrintf(_T("\n[BuildEngine] MAX DEPTH for %ls\n"), wCode);
        free(wCode);
        return "";
    }

    // ------------------------------------------------------------
    // ШАГ 2. Детектор циклов графа.
    // insert().second == false означает: такой код УЖЕ стоит в стеке
    // вызовов, т.е. мы рекурсивно вернулись к предку — цикл. Выходим
    // с пустым результатом; родительский вызов пропустит этого ребёнка
    // (в его ids просто не добавится XREF), сборка продолжится.
    // ------------------------------------------------------------
    if (!m_activeCodes.insert(code).second) {
        wchar_t* wCode = utf2uni(code.c_str());
        // %ls для wchar_t* (ANSI-printf с wide-аргументом = UB).
        acutPrintf(_T("\n[BuildEngine] CYCLE DETECTED at %ls (depth=%d) — skipped\n"), wCode, depth);
        free(wCode);
        return "";
    }

    std::string result;   // итоговый путь ('' = ошибка); заполняется ниже

    // ------------------------------------------------------------
    // ШАГ 3. Основная логика сборки. Все выходы — через break в
    // цикл-обёртку, чтобы гарантировать снятие активного кода (ШАГ 4)
    // на любой ветке, включая ранние возвраты при ошибках.
    // ------------------------------------------------------------
    do {
        // Загружаем нейрон по коду (использует уникальный индекс idx_code
        // по виртуальному столбцу `code`, добавленный в схеме БД, — поиск
        // за O(log N) вместо full table scan).
        TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
        if (!pNeuron) break;                 // нет записи / ошибка БД

        // Копируем данные во избежание висячего указателя: объект
        // принадлежит внутреннему пулу ядра и может быть освобождён.
        TrinityNeuron neuron = *pNeuron;
        delete pNeuron;

        // Подкаталог и путь — через единый хелпер FileManager
        std::string subdir = m_files.subdirForType(neuron.type);
        std::string filePath = m_files.getFilePath(neuron.code, subdir);

        // Если файл уже есть на диске (вырезан deleteProjectFiles для
        // другого проекта-потребителя либо создан ранее) — не пересобираем,
        // сразу кладём путь в кэш.
        if (m_files.fileExists(neuron.code, subdir)) {
            result = filePath;
            break;
        }

        // Строим базу нужного типа
        AcDbDatabase* db = nullptr;
        if (neuron.type == "detail") {
            db = buildDetail(neuron);
        }
        else {
            db = buildDwg(neuron, depth);
        }
        if (!db) break;                      // ошибка геометрии/wblock

        // Сохраняем (промежуточные папки создаются рекурсивно, путь — Unicode-безопасно)
        ensureDirectoryForFile(filePath);
        bool saved = m_files.saveDwg(db, filePath);
        delete db;                           // база больше не нужна — освобождаем сразу

        if (!saved) {
            // Файл не записан — НЕ кешируем путь, иначе последующие
            // attachXref получат несуществующий файл.
            break;
        }

        // Пауза после записи: некоторые версии AutoCAD/антивирусы держат
        // файл открытым краткое мгновение после saveAs; без паузы
        // немедленный attachXref иногда давал eFileAccessErr.
        // TODO: заменить на проверку блокировки файла (см. план улучшений).
        Sleep(200);

        result = filePath;                   // успех
    } while (false);

    // ------------------------------------------------------------
    // ШАГ 4. Снятие активного кода (обязательно на всех путях выхода —
    // иначе ложные срабатывания детектора циклов для «серебристых»
    // DAG-графов, где узел достижим из разных веток нециклически).
    // ------------------------------------------------------------
    m_activeCodes.erase(code);

    // ------------------------------------------------------------
    // ШАГ 5. Кешируем ТОЛЬКО успешный результат. Неудача может быть
    // временной (файл заблокирован, сеть до MySQL), поэтому другие
    // родители того же кода в этой же сборке получат шанс повторить
    // попытку, а не унаследуют забитый путь.
    // ------------------------------------------------------------
    if (!result.empty()) {
        m_builtCache[code] = result;
    }

    return result;
}

// ============================================================
// ОБРАБОТКА ВСЕХ PENDING-ПРОЕКТОВ
// ============================================================
// Результат работы — файлы DWG на диске (проекты, конструкции, детали).
// Вставка проекта в текущий чертёж сознательно НЕ выполняется: пользователю
// достаточно созданного файла проекта (см. примечание в начале файла).
// Параметр targetDb сохранён в сигнатуре для совместимости с вызывающим
// кодом (trinityProcess), но внутри не используется.
// ============================================================
int TrinityBuildEngine::processAllProjects(AcDbDatabase* /*targetDb*/) {
    auto projects = m_core.loadPendingProjects();
    if (projects.empty()) return 0;

    /*
    acutPrintf(_T("\n[BuildEngine] Processing %d projects...\n"),
        static_cast<int>(projects.size()));
    */

    for (auto& proj : projects) {
        // Рекурсивно удаляем все файлы, связанные с этим проектом
        // Это гарантирует, что сборка начнётся с чистого листа.
        // ВАЖНО: deleteProjectFiles также вычищает соответствующие записи
        // из m_builtCache — иначе кэш вернул бы пути к только что удалённым
        // файлам (см. реализацию).
        deleteProjectFiles(proj.code);

        // Полный сброс кэша перед каждым проектом.
        // Зачем: два разных проекта могут использовать ОДИН общий компонент
        // (например, стандартную планку). Формально его файл переиспользуем,
        // но deleteProjectFiles второго проекта уже мог удалить этот DWG с
        // диска (он рекурсивно проходит по всем детям), поэтому самый
        // простой и надёжный способ не вернуть «мёртвый» путь — начать
        // обработку каждого проекта с пустым кэшем. Стоимость — O(размер
        // кэша), пренебрежимо мала по сравнению с самой сборкой.
        m_builtCache.clear();

        // Создаём файл проекта (рекурсивно; повторные обращения к одним
        // и тем же кодам внутри этого вызова обслуживаются кэшем)
        std::string projectFilePath = ensureFileExists(proj.code, 0);

        if (projectFilePath.empty()) {
            wchar_t* wCode = utf2uni(proj.code.c_str());
            acutPrintf(_T("\n[BuildEngine] Failed to create project: %ls\n"), wCode);
            free(wCode);
            continue;
        }

        // Отмечаем done — файл создан
        m_core.markNeuronDone(proj.id);

        wchar_t* wCode = utf2uni(proj.code.c_str());
        acutPrintf(_T("\n[BuildEngine] Project done: %ls\n"), wCode);
        free(wCode);
    }

    return static_cast<int>(projects.size());
}

// ============================================================
// УДАЛЕНИЕ ФАЙЛОВ ПРОЕКТА (рекурсивно по детям)
// ============================================================
// Рекурсивно обходит дерево проекта и физически удаляет DWG-файлы всех
// его узлов (проект -> конструкции -> детали).
//
// ВАЖНО ПРО КЭШ: здесь кэш НЕ инвалидируется поштучно. Вызывающий код
// (processAllProjects) делает полный m_builtCache.clear() сразу после
// возврата из этой функции, до начала новой сборки. Это осознанное
// упрощение: deleteProjectFiles вызывается только оттуда, а частичная
// инвалидация (erase по каждому коду) лишь дублировала бы работу clear()
// с риском рассинхронизации при будущем изменении порядка вызовов.
// Если функция начнёт использоваться в других местах — добавить сюда
// m_builtCache.erase(code) рядом с удалением файла.
// ============================================================
void TrinityBuildEngine::deleteProjectFiles(const std::string& code) {
    // Пустой код — нечего удалять (защита от битых synapse.child_code)
    if (code.empty()) return;

    // Загружаем нейрон
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return;

    TrinityNeuron neuron = *pNeuron;
    delete pNeuron;

    // Подкаталог и путь к файлу — через единый хелпер FileManager
    std::string subdir = m_files.subdirForType(neuron.type);
    std::string filePath = m_files.getFilePath(neuron.code, subdir);

    // ------------------------------------------------------------
    // ЗАЩИТА ОТ ЦИКЛОВ ГРАФА.
    // deleteProjectFiles рекурсивно обходит граф связей; при цикле
    // A -> B -> ... -> A обход был бы бесконечным. Здесь множество
    // m_deletedCodes работает как «seen set» одного прохода удаления:
    // повторное посещение узла пропускается (его файл уже удалён выше
    // по стеку). Очистка множества — в самом конце функции, когда
    // завершается ВЕРХНИЙ уровень рекурсии (см. флаг isTopLevel ниже).
    // Проверка обязательна ДО рекурсивного спуска к детям — иначе
    // зацикливание не будет остановлено вообще.
    // ------------------------------------------------------------
    const bool isTopLevel = m_deletedCodes.empty();  // фиксируем ДО insert
    if (!m_deletedCodes.insert(code).second) {
        return;   // узел уже обработан в этом проходе — цикл/повтор, выходим
    }

    // Удаляем файл, если он существует
    if (m_files.fileExists(neuron.code, subdir)) {
        // Фикс AV: точный размер вместо wchar_t[512] — при переполнении
        // буфер оставался неинициализированным и _wunlink читал мусор.
        std::wstring pathW = utf8ToWide(filePath);
        if (!pathW.empty()) _wunlink(pathW.c_str());

        wchar_t* wCode = utf2uni(neuron.code.c_str());
        acutPrintf(_T("\n[BuildEngine] Deleted file: %ls.dwg\n"), wCode);
        free(wCode);
    }

    // Если это не деталь — рекурсивно удаляем детей
    // (защита от циклов — см. блок выше, проверка выполняется до спуска)
    if (neuron.type != "detail") {
        auto children = m_core.loadChildren(neuron.id);
        for (auto& syn : children) {
            deleteProjectFiles(syn.childCode);
        }
    }

    // Верхний уровень рекурсии завершён — весь проход удаления окончен,
    // очищаем «seen set» для следующего проекта (следующий вызов из
    // processAllProjects должен увидеть пустое множество).
    if (isTopLevel) m_deletedCodes.clear();
}