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
//   2. Слои (_bolt, _tag, слой материала) создаются ОДИН РАЗ — здесь,
//      до сборки геометрии; wblock переносит их в чистую базу вместе
//      с объектами, повторного создания слоёв после wblock НЕ требуется.
//   3. build() → солид (щит/планка/стенка)
//   4. appendAcDbEntity(solidId, solid) — БЕЗ close()
//   5. Если rib → болты (drawBoltMarkers)
//   6. Если sidewall → сверление (booleanOper + erase)
//   7. solid->close() — ПОСЛЕ всех операций
//   8. Атрибуты (DETAIL_CODE на слое _tag)
//   9. wblock → cleanDb
//  10. Назначение слоёв объектам в чистой базе (единственный механизм
//      переназначения): solid → материал, circle → _bolt, attr → _tag.
//      Слои к этому моменту в cleanDb уже существуют (п. 2), поэтому
//      setLayer не может упасть на eLayerNotFound.
// ============================================================
AcDbDatabase* TrinityBuildEngine::buildDetail(const TrinityNeuron& detail) {
    AcDbDatabase* tempDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
    if (!tempDb) return nullptr;

    // Слои создаются ОДИН РАЗ — до сборки геометрии. wblock переносит их
    // в чистую базу вместе с объектами, поэтому повторного создания слоёв
    // после wblock нет (была избыточная работа: тот же набор ensure*-вызовов
    // дублировался до и после wblock).
    std::string layer = TrinityLayerManager::layerName(detail.material);
    // Фикс AV: точный размер вместо wchar_t[256] (при переполнении буфер
    // оставался неинициализированным — setLayer читал мусор стека).
    std::wstring layerW = utf8ToWide(layer);
    TrinityLayerManager::createOrGetLayer(tempDb, detail.material);
    if (detail.category == "rib") {
        TrinityLayerManager::ensureBoltLayer(tempDb);
    }
    TrinityAttributeBuilder::ensureTagLayer(tempDb);

    // Строим геометрию
    AcDb3dSolid* solid = TrinityGeometryBuilder::build(detail);
    if (!solid) {
        delete tempDb;
        return nullptr;
    }

    // Назначаем слой материала (слой уже создан выше)
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
    // БОЛТЫ ДЛЯ ПЛАНОК (слой _bolt уже создан в начале функции)
    // ============================================================
    if (detail.category == "rib") {
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
    // АТРИБУТЫ (DETAIL_CODE на слое _tag, создан в начале функции)
    // ============================================================
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
    // ПЕРЕНАЗНАЧАЕМ СЛОИ ОБЪЕКТАМ В ЧИСТОЙ БАЗЕ
    // Единственный механизм работы со слоями: слои (_bolt, _tag, слой
    // материала) уже перенесены в cleanDb самим wblock из tempDb —
    // повторного createOrGetLayer/ensure* здесь НЕ было и нет.
    // setLayer выполняется по точным типам объектов (solid → материал,
    // circle → _bolt, attr → _tag), потому что wblock присваивает всем
    // перенесённым объектам слой "0".
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

    for (pIter->start(); !pIter->done(); pIter->step()) {
        AcDbEntity* pEnt = nullptr;
        if (pIter->getEntity(pEnt, AcDb::kForWrite) == Acad::eOk && pEnt) {
            if (pEnt->isKindOf(AcDb3dSolid::desc())) {
                if (!layerW.empty())
                    pEnt->setLayer(layerW.c_str());    // солид → материал
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

    for (auto& syn : children) {
        AcGePoint3d childPos = syn.position.toAcGe();

        std::string childFilePath = ensureFileExists(syn.childCode, depth + 1);
        if (childFilePath.empty()) {
            // ============================================================
            // СТРАТЕГИЯ «ЧЕСТНОЙ СБОРКИ» (защита от ложного done)
            // ============================================================
            // Было: проблемный ребёнок молча пропускался (continue), сборка
            // продолжалась и проект в итоге помечался 'done' с НЕПОЛНЫМ
            // комплектом — дефектный DWG расходился в производство.
            // Стало: отсутствие любого ребёнка = провал сборки узла.
            // Возвращаем пустой путь наверх по рекурсии; родитель тоже
            // проваливается, пока ошибка не дойдёт до processAllProjects,
            // где проект получит retry/ошибочный статус вместо 'done'.
            acutPrintf(_T("\n[BuildEngine] Child build failed: %ls — parent aborted\n"),
                       utf8ToWide(syn.childCode).c_str());
            delete tempDb;   // временная база с частично навешанными XREF не нужна
            return nullptr;
        }

        // НЕ держим Model Space открытым — attachXref сам его откроет
        AcDbObjectId childId = m_files.attachXref(
            childFilePath, syn.childCode, childPos, syn.rotation, tempDb);

        if (childId != AcDbObjectId::kNull) {
            ids.append(childId);
        }
        else {
            // Файл ребёнка есть, но вставить его не удалось (битый DWG,
            // блокировка, нехватка памяти) — это тоже неполная сборка.
            // Раньше просто логировали и шли дальше; теперь — провал узелa.
            acutPrintf(_T("\n[BuildEngine] attachXref returned kNull for %ls — parent aborted\n"),
                       utf8ToWide(syn.childCode).c_str());
            delete tempDb;
            return nullptr;
        }
    }

    if (ids.isEmpty()) {
        // Пустая коллекция детей означает одно из двух:
        //   1) у конструкции/проекта НЕТ ни одной связи synapse — сборка
        //      заведомо неполная (было: молча возвращали nullptr, но если
        //      файл уже лежал на диске, ensureFileExists считал бы его
        //      успехом и проект стал бы 'done' без содержимого);
        //   2) дети есть, но все они были «пустыми» кодами.
        // В обоих случаях — явная ошибка с диагностикой.
        acutPrintf(_T("\n[BuildEngine] No children resolved for %ls — treated as FAILURE\n"),
                   utf8ToWide(neuron.code).c_str());
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
// ОЖИДАНИЕ ДОСТУПНОСТИ ФАЙЛА ПОСЛЕ ЗАПИСИ (замена Sleep(200))
// ============================================================
// Проблематика: сразу после saveAs файл может мгновение оставаться за-
// блокированным — AutoCAD дописывает хвост, антивирусный minifilter от-
// крывает его на скан, сетевой шаровой кэш сбрасывает handle. Старый код
// решал это вслепую: Sleep(200) НА ГЛАВНОМ ПОТОКЕ AutoCAD (тик таймера),
// причём для КАЖДОГО узла рекурсии. Итог: 100 деталей = 20 секунд «мёрт-
// вого» времени UI при том, что в 99% случаев файл был готов уже через
// ~5 мс после записи.
//
// Подход: активная проверка вместо пассивной паузы. Пытаемся переоткрыть
// файл с теми же sharing-флагами, которые потребуются attachXref (чте-
// ние при разрешённых read/write-шарингах соседей). Успех открытия оз-
// начает: ни один драйвер/процесс не держит файл эксклюзивно — можно
// строить дальше. Если файл ещё залочен, делаем КОРОТКИЕ итерации ожидания.
//
// Про сон между попытками: Sleep() здесь — это yield'ы суммарно НЕ БО-
// ЛЬШЕ kMaxWaitMs (150 мс) на узел, т.е. худший случай вдвое короче ста-
// рых фиксированных 200 мс, а типичный (файл готов сразу) — 0 мс. Пол-
// ностью убрать ожидание нельзя: без него мы получаем busy-wait, кото-
// рый на главном потоке хуже корректного короткого sleep. Радикальное
// решение (перенос сборки на фоновый поток) — вне объёма этого фикса;
// здесь важно, что ВРЕМЯ ОЖИДАНИЯ ТЕПЕРЬ ПРОПОРЦИОНАЛЬНО РЕАЛЬНОЙ БЛО-
// КИРОВКЕ, а не числу узлов графа.
//
// Возвращает true, если файл доступен для чтения; false — если по ис-
// черпании лимита остаётся залоченным (вызывающий код трактовает это
// как временную ошибку сборки: не кеширует путь, следующий тик повторит).
static bool fileReadyForRead(const std::string& filePathUtf8) {
    // Суммарный потолок ожидания одного файла, мс. Подобран так, чтобы
    // даже полностью «засонный» проход был дешевле старого Sleep(200):
    // 3 итерации x (попытка + 40 мс) ≈ 120-150 мс в худшем случае.
    const DWORD kMaxWaitMs   = 150;
    const DWORD kPollStepMs  = 40;    // шаг опроса: достаточно частый
                                      // для capture-скана AV, но не busy-loop

    const std::wstring pathW = utf8ToWide(filePathUtf8);
    if (pathW.empty()) return false;  // битый UTF-8 — считаем недоступным

    const ULONGLONG deadline = GetTickCount64() + kMaxWaitMs;
    for (;;) {
        // Открытие именно с dwShareReadWrite — проверяем «никто не дер-
        // жит эксклюзивно», что и нужно последующему attachXref.
        HANDLE h = CreateFileW(pathW.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return true;              // файл реально читается — готовы
        }

        // Файл существует, но открыться не дал? Отличаем «ещё залочен»
        // от «вообще нет/битый путь»:sharing violation и denied — ждём,
        // остальные коды — бессмысленно (возвращаем false немедленно).
        const DWORD err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION &&
            err != ERROR_ACCESS_DENIED  &&
            err != ERROR_LOCK_VIOLATION) {
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;   // лимит ожидания исчерпан

        // Не спать дольше дедлайна: последняя итерация может быть ко-
        // робче шага, чтобы суммарное ожидание честно упиралось в
        // kMaxWaitMs, а не переполняло его на целый шаг опроса.
        const ULONGLONG left = deadline - now;
        Sleep(left < kPollStepMs ? static_cast<DWORD>(left) : kPollStepMs);
    }
}

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
        acutPrintf(_T("\n[BuildEngine] MAX DEPTH for %ls\n"), utf8ToWide(code).c_str());
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
        // Цикл в графе — это ОШИБКА ДАННЫХ, а не временный сбой: возврат
        // пустого пути здесь приводит к провалу всей цепочки родителей и,
        // в конечном счёте, к markNeuronError проекта (лимит попыток затем
        // заморозит его в 'error' — см. processAllProjects).
        // %ls для wchar_t*-аргумента (ANSI-printf с wide-аргументом = UB).
        acutPrintf(_T("\n[BuildEngine] CYCLE DETECTED at %ls (depth=%d) — build fails\n"),
                   utf8ToWide(code).c_str(), depth);
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
        auto optNeuron = m_core.loadNeuronByCode(code);
        if (!optNeuron) break;               // нет записи / ошибка БД

        // optional возвращает карточку по значению — копия делается
        // перемещением (move), сырой указатель и delete больше не нужны.
        TrinityNeuron neuron = std::move(*optNeuron);

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

        // ------------------------------------------------------------
        // Пауза после записи — УБРАНА (был Sleep(200) на главном по-
        // токе AutoCAD).
        // Зачем был: некоторые версии AutoCAD/антивирусы держат файл
        // открытым мгновение после saveAs, и немедленный attachXref
        // иногда давал eFileAccessErr.
        // Чем плохо: ensureFileExists вызывается РЕКУРСИВНО для каждого
        // узла графа на ГЛАВНОМ потоке в составе тика таймера. 200 мс x
        // N узлов = десятки секунд непрерывной блокировки message pump:
        // UI AutoCAD висел («Not Responding») ровно столько, сколько
        // длилась сборка, без какой-либо полезной работы в это время.
        // Чем заменено: fileReadyForRead() — активная проверка того,
        // что файл реально доступен для чтения (переоткрытие через
        // CreateFile с FILE_FLAG_SEQUENTIAL_SCAN; антивирусный filter
        // driver блокирует именно sharing-режимы). Ждём ровно столько,
        // сколько нужно (обычно 0 попыток — файл уже готов), максимум
        // ~150 мс на узел, а не фиксированные 200 мс на каждый.
        // ------------------------------------------------------------
        if (!fileReadyForRead(filePath)) {
            // Файл так и остался заблокированным (вечный скан антиви-
            // руса, сетевая шара упала). НЕ кешируем путь: следующий
            // тик пересоберёт узел, когда блокировка спадёт.
            acutPrintf(_T("\n[BuildEngine] File locked after save, will retry next tick: %hs\n"),
                       filePath.c_str());
            break;
        }

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
//
// ============================================================
// МАШИН СОСТОЯНИЙ ПРОЕКТА (защита от бесконечных перезапусков)
// ============================================================
// Было: при любой ошибке сборки статус оставался 'pending' → таймер (5 сек)
// вечно перезапускал заведомо провальную сборку; при частичных ошибках
// проект всё равно получал 'done' (несовместимо со «стратегией честной
// сборки» в buildDwg/ensureFileExists).
//
// Стало (все переходы выполняются через TrinityCore):
//
//   pending / building ──(тик таймера: loadPendingProjects берёт оба)──▶
//        │ setBuildStatus('building') перед началом сборки
//        ▼
//    [сборка ensureFileExists(code)]
//        │
//        ├── успех (файл проекта создан, все дети собраны)
//        │        ▼
//        │     markNeuronDone: status='done', build_attempts=0, build_error удалён
//        │     (дальнейших перезапусков нет — 'done' из выборки исключён)
//        │
//        └── провал (пустой путь: цикл в графе, битый synapse, ошибка БД,
//                    отсутствие детей, ошибка wblock/saveDwg)
//                 ▼
//              markNeuronError: build_attempts++ и
//                 attempts < MAX_BUILD_ATTEMPTS → status='pending'  (retry через 5 сек)
//                 attempts >= MAX_BUILD_ATTEMPTS → status='error'   (ХОП! перезапуски
//                    прекращаются; возврат в очередь — только ручным сбросом статуса)
//
//   Отдельный случай: если сам UPDATE статуса не прошёл (БД недоступна) —
//   прерываем обработку всей пачки (break): продолжать бессмысленно, т.к.
//   loadPendingProjects уже вернул данные из живого соединения, а запись
//   упала — вероятно, соединение умерло в процессе; следующий тик
//   ensureConnected() переподключится и продолжит очередь.
//
// Промежуточный статус 'building' нужен для восстановления после ЖЁСТКИХ
// отказов (крах AutoCAD/плагина посреди сборки): такой проект останется в
// 'building' и будет подобран следующим тиком — очередь не зависает.
// Счётчик попыток при этом НЕ инкрементируется (инкремент только в
// markNeuronError), поэтому аварийные перезапуски не «съедают» лимит.
// ============================================================

// Максимальное число автоматических попыток сборки одного проекта.
// После достижения лимита проект переходит в 'error' и ждёт ручного
// разбора (см. блок СБРОС СТАТУСА ПРОЕКТА в database.sql).
static const int MAX_BUILD_ATTEMPTS = 3;

int TrinityBuildEngine::processAllProjects(AcDbDatabase* /*targetDb*/) {
    // Совместимость: старый «полный» вызов без ограничения по времени.
    // Используется только явными командами (ручная обработка очереди);
    // ТАЙМЕР же обязан ходить через processTick() с бюджетом — иначе
    // длинная сборка блокирует message pump AutoCAD (зависший UI).
    return processAllProjectsInternal(-1);
}

// ============================================================
// ТИК С БЮДЖЕТОМ — основная реализация обработки очереди
// ============================================================
// budgetMs < 0 = без ограничений (legacy-режим processAllProjects).
// budgetMs >= 0 = после каждого ЗАВЕРШЁННОГО проекта проверяем, сколько
// времени уже потрачено в этом тике; при исчерпании бюджета прерываем-
// ся на границе проекта. Прерывание ВНУТРИ сборки одного проекта со-
// знанно НЕ делается: полу-собранный граф оставил бы гору временных
// DWG и неконсистентный статус, а идемпотентная пересборка целого про-
// екта следующим тиком дешевле и надёжнее.
int TrinityBuildEngine::processTick(int budgetMs) {
    return processAllProjectsInternal(budgetMs);
}

// Общий код обоих входов. Отдельная приватная функция нужна, чтобы
// публичные API остались тонкими и документированными.
int TrinityBuildEngine::processAllProjectsInternal(int budgetMs) {
    // Момент входа в тик — от него считаем израсходованный бюджет.
    // GetTickCount64 монотонен, дёшев (<1 мкс), не требует синхрониза-
    // ции; для точности измерений длительности проектов хватает с за-
    // пасом (разрешение ~15 мс — типичная сборка проекта дольше).
    const ULONGLONG startTick = GetTickCount64();

    auto projects = m_core.loadPendingProjects();
    if (projects.empty()) return 0;

    /*
    acutPrintf(_T("\n[BuildEngine] Processing %d projects...\n"),
        static_cast<int>(projects.size()));
    */

    for (auto& proj : projects) {
        // ------------------------------------------------------------
        // ШАГ 1. Переводим проект в 'building' ДО начала работы.
        // Зачем до: если сборка оборвётся аварийно (крах процесса),
        // проект останется в 'building' и будет подхвачен следующим
        // тиком (loadPendingProjects выбирает pending+building).
        // Не удалось записать статус = БД недоступна — прерываем пачку
        // (continue/обработка остальных проектов невозможна корректно:
        // финальные статусы тоже не запишутся).
        // ------------------------------------------------------------
        if (!m_core.setBuildStatus(proj.id)) {
            acutPrintf(_T("\n[BuildEngine] DB unavailable, aborting batch\n"));
            break;
        }

        // ------------------------------------------------------------
        // ШАГ 1.5. Мгновенная проверка доступности БД (защита UI).
        // setBuildStatus выше уже прошёл ensureConnected(), но между
        // проектами большой пачки сервер мог лечь (рестарт службы, об-
        // рыв канала). Без этой проверки каждый следующий проект повто-
        // рял бы ту же сетевую паузу до таймаута на ГЛАВНОМ потоке; с
        // ней тик прерывается максимум одной задержкой, а при открытом
        // куполе — вообще мгновенно. Проекты остаются в 'building' и
        // корректно подхватываются после восстановления связи.
        // ------------------------------------------------------------
        if (!m_core.ensureConnected()) {
            acutPrintf(_T("\n[BuildEngine] DB became unavailable mid-batch, tick aborted\n"));
            break;
        }

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

        // ------------------------------------------------------------
        // ШАГ 2. Создаём файл проекта (рекурсивно; повторные обращения
        // к одним и тем же кодам внутри этого вызова обслуживаются кэшем).
        // Благодаря «честной сборке» непустой результат означает: файл
        // проекта существует И все потомки собраны без пропусков.
        // Пустой результат = провал любой стадии (диагностика уже
        // напечатана на месте ошибки в рекурсии).
        // ------------------------------------------------------------
        std::string projectFilePath = ensureFileExists(proj.code, 0);

        // Wide-копия кода проекта для всех логов ниже: один вызов
        // конвертера вместо трёх повторных.
        const std::wstring wCode = utf8ToWide(proj.code);

        if (projectFilePath.empty()) {
            // --------------------------------------------------------
            // ШАГ 3a. Провал: фиксируем неудачную ПОПЫТКУ.
            // markNeuronError увеличит build_attempts и сам решит,
            // возвращать ли проект в 'pending' или заморозить его в
            // 'error' (лимит MAX_BUILD_ATTEMPTS). Именно этот шаг
            // обрывает бесконечные перезапуски каждые 5 секунд.
            // Мусорный частичный файл проекта (если он всё же успел
            // появиться) удаляем, чтобы next-проход начал с чистого
            // листа и fileExists-быстрый путь не принял брак за успех.
            // --------------------------------------------------------
            acutPrintf(_T("\n[BuildEngine] Failed to create project: %ls (attempt recorded)\n"), wCode.c_str());

            std::string projPath = m_files.getFilePathForNeuron(proj.code, proj.type);
            if (!projPath.empty()) _wunlink(utf8ToWide(projPath).c_str());

            if (!m_core.markNeuronError(proj.id, MAX_BUILD_ATTEMPTS,
                                         "build failed or incomplete (see console log)")) {
                // Статус не записан (БД отвалилась прямо сейчас) — проект
                // останется в 'building' и будет перебран тиком позже;
                // further processing of the batch is pointless.
                acutPrintf(_T("\n[BuildEngine] Cannot record failure, aborting batch\n"));
                break;
            }
            continue;
        }

        // --------------------------------------------------------
        // ШАГ 3b. Успех: building -> done + сброс счётчика попыток.
        // Если UPDATE не прошёл (обрыв БД в момент записи), проект
        // останется в 'building' и будет пересобран следующим тиком —
        // это безопасно: повторная сборка идемпотентна (deleteProjectFiles
        // + пересоздание), а ложного 'done' мы не допускаем.
        // --------------------------------------------------------
        if (!m_core.markNeuronDone(proj.id)) {
            acutPrintf(_T("\n[BuildEngine] Project built but cannot mark done: %ls, aborting batch\n"), wCode.c_str());
            break;
        }

        acutPrintf(_T("\n[BuildEngine] Project done: %ls\n"), wCode.c_str());

        // ------------------------------------------------------------
        // ШАГ 4. Бюджет тика (защита UI от длинных очередей).
        // Проект завершён ЦЕЛИКОМ — безопасная граница для прерывания.
        // Если время тика исчерпано, остальные проекты остаются в 'pend-
        // ing' и будут обработаны следующим тиком через 5 секунд; для
        // больших сборок это единственная преграда между «AutoCAD не от-
        // вечает» и отзывчивым интерфейсом.
        // budgetMs < 0 = режим без ограничений (legacy processAllProjects).
        // ------------------------------------------------------------
        if (budgetMs >= 0 &&
            GetTickCount64() - startTick >= static_cast<ULONGLONG>(budgetMs)) {
            const int remaining =
                static_cast<int>(&proj - projects.data()) + 1;   // ещё не взятых в работу
            acutPrintf(_T("\n[BuildEngine] Tick budget (%d ms) exhausted, %d project(s) deferred to next tick\n"),
                budgetMs, remaining);
            break;
        }
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

    // Загружаем нейрон (std::optional — без сырых указателей и ручного delete)
    auto optNeuron = m_core.loadNeuronByCode(code);
    if (!optNeuron) return;

    TrinityNeuron neuron = std::move(*optNeuron);

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

        acutPrintf(_T("\n[BuildEngine] Deleted file: %ls.dwg\n"), utf8ToWide(neuron.code).c_str());
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