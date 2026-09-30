// TrinitySymbolUtils.h
//
// Замена несуществующего заголовка ObjectARX <acdbSymutil.h>: тот вызывал
// C1083 ("Не удается открыть файл включение") при компиляции StdAfx.cpp.
// В поставке ObjectARX (включая 2026) нет ни acdbSymutil.h, ни функции
// eraseAcDbSymbolRecord() — они были вымышленными. Здесь — собственная
// inline-реализация той же функциональности поверх штатного API.
//
// Используется в TrinityFileManager::insertXref() для безопасного удаления
// устаревших XREF-записей таблицы блоков БЕЗ ручного upgradeOpen():
// запись открывается на запись сама, все коды ошибок проверяются, а перед
// erase() выполняётся отвязка внешнего файла (detach), чтобы AutoCAD не
// пытался разрешить уже несуществующий DWG.
//
// ТРЕБУЕТСЯ: до этого заголовка должны быть включены StdAfx.h
// (dbsymtb.h — классы таблиц символов, dbents.h — AcDbBlockTableRecord).
#pragma once

#include <dbsymtb.h>   // AcDbSymbolTable / AcBlockTable / AcDbBlockTableRecord
#include <dbents.h>

// Удаляет запись символической таблицы (блок, слой, линетип, текст.стиль
// и т.п.) по её AcDbObjectId. Возвращает Acad::eOk при успехе, иначе —
// код ошибки ObjectARX (вызывающий код обязан его проверить).
inline Acad::ErrorStatus eraseAcDbSymbolRecord(const AcDbObjectId& recId)
{
    if (recId.isNull()) return Acad::eNullObjectId;

    // Открываем саму запись на запись — никакого upgradeOpen() над
    // родительской таблицей, никаких непроверенных вызовов.
    AcDbSymbolTableRecord* pRec = nullptr;
    Acad::ErrorStatus es = acdbOpenAcDbSymbolTableRecord(
        pRec, recId, AcDb::kForWrite);
    if (es != Acad::eOk) return es;

    if (!pRec) return Acad::eFail;   // защита от неожиданных API

    // Если это запись блока-XREF — сначала отвязываем внешний файл,
    // иначе удалённая "на живую" ссылка оставляет базу в состоянии,
    // при котором последующая вставка под тем же именем падает.
    AcDbBlockTableRecord* pBtr =
        AcDbBlockTableRecord::cast(pRec);
    if (pBtr && pBtr->isFromExternalReference()) {
        const Acad::ErrorStatus detachEs = pBtr->detach();
        // eWasOpenForWrite / eNotFromExternalReference здесь не фатальны:
        // запись уже открыта нами на запись, а статус XREF проверен выше.
        if (detachEs != Acad::eOk &&
            detachEs != Acad::eWasOpenForWrite) {
            pRec->close();
            return detachEs;
        }
    }

    es = pRec->erase();          // помечает объект удалённым в транзакции
    pRec->close();               // обязательное закрытие при любом исходе
    return es;
}
