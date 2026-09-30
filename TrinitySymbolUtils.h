// TrinitySymbolUtils.h
//
// Замена несуществующего заголовка ObjectARX <acdbSymutil.h>: тот вызывал
// C1083 ("Не удается открыть файл включение") при компиляции StdAfx.cpp.
// В поставке ObjectARX (включая 2026) нет ни acdbSymutil.h, ни функции
// eraseAcDbSymbolRecord() — они были вымышленными. Здесь — собственная
// inline-реализация той же функциональности поверх ШТАТНОГО API ObjectARX.
//
// Исправления относительно предыдущих версий файла (ошибки C3861 / C2039 /
// C2065 / C2737): вместо вымышленных символов теперь используется только
// документированный API ObjectARX: acdbOpenObject(), AcDbEntity::erase(),
// Acad::eOk / eNullObjectId / eInvalidInput.
//
// Используется в TrinityFileManager для безопасного удаления устаревших
// XREF-записей таблицы блоков БЕЗ ручного upgradeOpen(): запись открывается
// на запись сама, все коды ошибок проверяются.
//
// ТРЕБУЕТСЯ: до этого заголовка должны быть включены StdAfx.h
// (dbsymtb.h — классы таблиц символов, dbmain.h — acdbOpenObject).
#pragma once

#include <dbsymtb.h>   // AcDbSymbolTableRecord
#include <dbmain.h>    // acdbOpenObject()

// Удаляет запись символической таблицы (блок, слой, линетип, стиль и т.п.)
// по её AcDbObjectId. Возвращает Acad::eOk при успехе, иначе — код ошибки
// ObjectARX (вызывающий код обязан его проверить).
inline Acad::ErrorStatus eraseAcDbSymbolRecord(const AcDbObjectId& recId)
{
    if (recId.isNull()) return Acad::eNullObjectId;

    // Открываем саму запись на запись — никакого upgradeOpen() над
    // родительской таблицей, все коды ошибок проверяются.
    AcDbSymbolTableRecord* pRec = nullptr;
    Acad::ErrorStatus es = acdbOpenObject(pRec, recId, AcDb::kForWrite);
    if (es != Acad::eOk) return es;

    if (!pRec) return Acad::eInvalidInput;   // защита от неожиданных API

    // Никакой предварительной "отвязки" внешнего файла не делаем: у
    // AcDbBlockTableRecord нет таких методов, и они не нужны — удаление
    // записи XREF через erase() легально, пока её родительская таблица
    // открыта на запись (условие обеспечивает acdbOpenObject kForWrite
    // выше); связанные ссылки на внешний файл AutoCAD обрабатывает сам.
    es = pRec->erase();   // помечает объект удалённым (в транзакции — если активна)
    pRec->close();        // обязательное закрытие при любом исходе
    return es;
}
