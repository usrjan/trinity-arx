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

    // Технические слои
    static void ensureTagLayer(AcDbDatabase* db);   // _tag
    static void ensureBoltLayer(AcDbDatabase* db);  // _bolt

    static constexpr const char* LAYER_TAG = "_tag";
    static constexpr const char* LAYER_BOLT = "_bolt";
};