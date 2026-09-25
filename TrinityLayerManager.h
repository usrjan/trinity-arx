// TrinityLayerManager.h
#pragma once
#include "StdAfx.h"

// ============================================
// Управление слоями документа AutoCAD:
//  - TRINITY_MAT_<material> — слои материалов
//  - _tag  — слой атрибутов DETAIL_CODE (красный, выключен)
//  - _bolt — слой маркеров болтов (красный, выключен)
// Все методы статические и работают с переданной базой.
// ============================================
class TrinityLayerManager {
public:
    static constexpr const wchar_t* LAYER_TAG  = L"_tag";
    static constexpr const wchar_t* LAYER_BOLT = L"_bolt";

    // Имя слоя по материалу: "TRINITY_MAT_PLYWOOD-FSF"
    static std::string layerName(const std::string& materialCode);

    // ACI-цвет слоя по материалу
    static int colorIndex(const std::string& materialCode);

    // Создать/получить слой материала. kNull при ошибке.
    static AcDbObjectId createOrGetLayer(AcDbDatabase* db,
                                         const std::string& materialCode);

    // Технические слои (_tag / _bolt): красный, выключен
    static void ensureTagLayer(AcDbDatabase* db);
    static void ensureBoltLayer(AcDbDatabase* db);

private:
    static void ensureHiddenRedLayer(AcDbDatabase* db, const wchar_t* name);
};
