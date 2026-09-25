// TrinityLayerManager.h
#pragma once
#include "StdAfx.h"

class TrinityLayerManager {
public:
    // Имя слоя по материалу
    static std::string layerName(const std::string& materialCode);

    // Цвет слоя по материалу
    static int colorIndex(const std::string& materialCode);

    // Создать/получить слой материала
    static AcDbObjectId createOrGetLayer(AcDbDatabase* db, const std::string& materialCode);

    // Технические слои (общая реализация — ensureTechLayer)
    static void ensureTagLayer(AcDbDatabase* db);   // _tag
    static void ensureBoltLayer(AcDbDatabase* db);  // _bolt

private:
    // Общая логика создания технического слоя (красный, выключен)
    static void ensureTechLayer(AcDbDatabase* db, const wchar_t* layerName);

public:

    static constexpr const char* LAYER_TAG = "_tag";
    static constexpr const char* LAYER_BOLT = "_bolt";

    // Широкосимвольные варианты для setLayer() (ObjectARX, юникод-сборка)
    static constexpr const wchar_t* LAYER_TAG_W = L"_tag";
    static constexpr const wchar_t* LAYER_BOLT_W = L"_bolt";
};