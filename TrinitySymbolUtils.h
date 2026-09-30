// TrinitySymbolUtils.h
//
// Замена несуществующего заголовка ObjectARX <acdbSymutil.h>: тот вызывал
// C1083 ("Не удается открыть файл включение") при компиляции StdAfx.cpp.
// В поставке ObjectARX (включая 2026) нет ни acdbSymutil.h, ни функции
// eraseAcDbSymbolRecord() — они были вымышленными. Здесь — собственная
// inline-реализация той же функциональности поверх ШТАТНОГО API ObjectARX.
//
// Исправления относительно предыдущей версии файла (ошибки C3861 / C2039 /
// C2065 / C2737):
//   * acdbOpenAcDbSymbolTableRecord() — такой функции в ObjectARX нет;
//     запись открывается стандартным acdbOpenObject() (объявлен в dbmain.h);
//   * Acad::eFail — такого члена в enum Acad::ErrorStatus нет; вместо него
//     возвращается реальный код ошибки открытия или Acad::eInvalidInput;
//   * AcDbBlockTableRecord::detach() — метода с таким именем нет;
//     "отвязка" внешнего файла выполняется установкой имени/пути XREF
//     в пустую строку (setXrefDwgName(_T(""))), что переводит запись из
//     external-reference в ordinary block без удаления содержимого блока.
//
// Используется в TrinityFileManager для безопасного удаления устаревших
// XREF-записей таблицы блоков БЕЗ ручного upgradeOpen(): запись открывается
// на запись сама, все коды ошибок проверяются.
//
// ТРЕБУЕТСЯ: до этого заголовка должны быть включены StdAfx.h
// (dbsymtb.h — классы таблиц символов, dbents.h — AcDbBlockTableRecord).
#pragma once

#include <dbsymtb.h>   // AcDbSymbolTableRecord / AcDbBlockTableRecord
#include <dbents.h>
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

    // Если это запись блока-XREF — сначала "отвязываем" внешний файл
    // (очищаем имя DWG-ссылки), иначе удалённая "на живую" ссылка оставляет
    // базу в состоянии, при котором последующая вставка под тем же именем
    // падает. setXrefDwgName() допустим только когда запись открыта на
    // запись — условие обеспечено выше.
    AcDbBlockTableRecord* pBtr = AcDbBlockTableRecord::cast(pRec);
    if (pBtr && pBtr->isFromExternalReference()) {
        const Acad::ErrorStatus detachEs =
            pBtr->setXrefDwgName(_T(""));
        if (detachEs != Acad::eOk) {
            pRec->close();
            return detachEs;
        }
    }

    es = pRec->erase();   // помечает объект удалённым (в транзакции — если активна)
    pRec->close();        // обязательное закрытие при любом исходе
    return es;
}
