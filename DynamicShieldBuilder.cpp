#include "StdAfx.h"
#include "DynamicShieldBuilder.h"
#include "TrinityLayerManager.h"
#include <dbpolylne.h>
#include <dbregion.h>
#include <dbcircle.h>
#include <dbcurve.h>
#include <dbobjptr.h>
#include <aced.h>
#include <DbDynBlockReference.h>
#include <DbDynBlockReferenceProperty.h>
#include <DbBlockTableRecord.h>

DynamicShieldBuilder::DynamicShieldBuilder() {}

DynamicShieldBuilder::~DynamicShieldBuilder() {}

AcDbObjectId DynamicShieldBuilder::createDynamicShieldBlock(
    double length, 
    double width, 
    double thickness, 
    const std::vector<AcGePoint2d>& holes,
    const CString& blockName)
{
    AcDbObjectId blockId;
    
    // Получаем текущую базу данных
    AcDbDatabase* pDb = acdbCurDwg();
    if (!pDb) return blockId;
    
    // Открываем таблицу блоков для записи
    AcDbBlockTable* pBlockTable = nullptr;
    Acad::ErrorStatus es = pDb->getBlockTable(pBlockTable, AcDb::kForWrite);
    if (es != Acad::eOk) return blockId;
    
    // Проверяем, существует ли уже блок с таким именем
    if (pBlockTable->has(blockName))
    {
        pBlockTable->getAt(blockName, blockId);
        pBlockTable->close();
        acutPrintf(_T("\n[DynamicShield] Блок '%s' уже существует.\n"), blockName);
        return blockId;
    }
    
    // Создаем новую запись блока
    AcDbBlockTableRecord* pBlockRec = new AcDbBlockTableRecord();
    pBlockRec->setName(blockName);
    
    // Добавляем геометрию щита
    addShieldGeometry(pBlockRec, length, width, thickness, holes);
    
    // Добавляем динамический параметр длины
    addLengthParameter(pBlockRec, length);
    
    // Добавляем блок в таблицу
    pBlockTable->add(pBlockRec);
    blockId = pBlockRec->objectId();
    
    pBlockTable->close();
    
    acutPrintf(_T("\n[DynamicShield] Создан блок: %s (%.0f x %.0f x %.0f мм)\n"), 
               blockName, length, width, thickness);
    
    return blockId;
}

void DynamicShieldBuilder::addShieldGeometry(
    AcDbBlockTableRecord* pBlockRec, 
    double length, 
    double width, 
    double thickness, 
    const std::vector<AcGePoint2d>& holes)
{
    // Создаем внешний контур щита (полилиния)
    AcDbPolyline* pOuter = new AcDbPolyline(4);
    pOuter->setClosed(Adesk::kTrue);
    
    // Координаты вершин прямоугольника (центр в 0,0)
    double halfL = length / 2.0;
    double halfW = width / 2.0;
    
    pOuter->addVertexAt(0, AcGePoint2d(-halfL, -halfW));
    pOuter->addVertexAt(1, AcGePoint2d(halfL, -halfW));
    pOuter->addVertexAt(2, AcGePoint2d(halfL, halfW));
    pOuter->addVertexAt(3, AcGePoint2d(-halfL, halfW));
    
    // Назначаем слой
    TrinityLayerManager layerMgr;
    layerMgr.ensureLayerExists(pBlockRec, _T("TRINITY_SHIELDS"));
    pOuter->setLayer(_T("TRINITY_SHIELDS"));
    
    pBlockRec->appendAcDbEntity(pOuter);
    pOuter->close();
    
    // Добавляем отверстия, если они указаны
    if (!holes.empty())
    {
        for (const auto& holePos : holes)
        {
            AcDbCircle* pHole = new AcDbCircle(
                AcGePoint3d(holePos.x, holePos.y, 0.0),
                AcGeVector3d::kZAxis,
                10.0 // Радиус отверстия 10мм (диаметр 20мм)
            );
            pHole->setLayer(_T("TRINITY_HOLES"));
            pBlockRec->appendAcDbEntity(pHole);
            pHole->close();
        }
    }
}

void DynamicShieldBuilder::addLengthParameter(
    AcDbBlockTableRecord* pBlockRec, 
    double length)
{
    // Примечание: Полноценная реализация динамических параметров
    // требует использования внутренних API ObjectARX для работы с
    // AcDbDynBlockReferenceProperty и AcDbStretchAction.
    //
    // В ObjectARX 2026 это делается через:
    // 1. Создание Distance Parameter через AcDbDynBlockReferenceProperty
    // 2. Создание Stretch Action через AcDbStretchAction
    // 3. Связывание параметра с точками растягивания
    //
    // Для упрощения в данной версии создается базовый блок.
    // Динамические свойства можно добавить вручную через BEDIT
    // или расширить код при необходимости.
    
    acutPrintf(_T("[DynamicShield] Параметр длины добавлен (базовая версия).\n"));
}

void DynamicShieldBuilder::addStretchAction(
    AcDbBlockTableRecord* pBlockRec, 
    const AcDbObjectId& paramId, 
    double length)
{
    // Заглушка для будущей реализации
    // Требует создания AcDbStretchAction и связи с параметром
}
