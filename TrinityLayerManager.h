// TrinityLayerManager.h
#pragma once
#include "StdAfx.h"

class TrinityLayerManager {
public:
    static std::string layerName(const std::string& materialCode);
    static int colorIndex(const std::string& materialCode);
    static AcDbObjectId createOrGetLayer(AcDbDatabase* db, const std::string& materialCode);
    static void ensureTagLayer(AcDbDatabase* db);
    static void ensureBoltLayer(AcDbDatabase* db);
    static constexpr const char* LAYER_TAG = "_tag";
    static constexpr const char* LAYER_BOLT = "_bolt";
};