#pragma once

#include <dbblockreference.h>
#include <dbdict.h>
#include <dbxrecard.h>
#include <geassign.h>
#include <acdbcurv.h>
#include <dblayer.h>
#include <aced.h>
#include <rxobject.h>
#include <dbdynblk.h>
#include <DbBlockTableRecord.h>
#include <DbDynBlockReferenceProperty.h>
#include <DbDynBlockReference.h>
#include <vector>

/// <summary>
/// Класс для создания динамических щитов опалубки.
/// Позволяет управлять размерами через палитру свойств AutoCAD.
/// </summary>
class DynamicShieldBuilder
{
public:
    DynamicShieldBuilder();
    ~DynamicShieldBuilder();

    /// <summary>
    /// Создает динамический блок щита и добавляет его в базу данных.
    /// </summary>
    /// <param name="length">Начальная длина щита (мм)</param>
    /// <param name="width">Ширина щита (мм)</param>
    /// <param name="thickness">Толщина щита (мм)</param>
    /// <param name="holes">Массив отверстий (координаты X,Y относительно центра)</param>
    /// <param name="blockName">Имя блока</param>
    /// <returns>ObjectId созданного блока</returns>
    static AcDbObjectId createDynamicShieldBlock(
        double length, 
        double width, 
        double thickness, 
        const std::vector<AcGePoint2d>& holes,
        const CString& blockName);

private:
    /// <summary>
    /// Создает геометрию щита (контуры).
    /// </summary>
    static void addShieldGeometry(AcDbBlockTableRecord* pBlockRec, double length, double width, double thickness, const std::vector<AcGePoint2d>& holes);

    /// <summary>
    /// Добавляет линейный параметр длины (Distance Parameter).
    /// </summary>
    static void addLengthParameter(AcDbBlockTableRecord* pBlockRec, double length);

    /// <summary>
    /// Добавляет действие растягивания (Stretch Action).
    /// </summary>
    static void addStretchAction(AcDbBlockTableRecord* pBlockRec, const AcDbObjectId& paramId, double length);
};
