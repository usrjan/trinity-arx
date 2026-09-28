// TrinityCommands.h
#pragma once
#include "StdAfx.h"
#include "TrinityBuildEngine.h"

extern TrinityBuildEngine* g_engine;
extern UINT_PTR g_timerId;

void trinityStart();
void trinityStop();
void trinityProcess();

// Отладочная команда для этапа разработки: рисует в текущем открытом
// чертеже все планки из базы (neuron.data.category == 'rib').
void trinityRib();