// TrinityCommands.h
#pragma once
#include "StdAfx.h"
#include "TrinityBuildEngine.h"

extern TrinityBuildEngine* g_engine;
extern UINT_PTR g_timerId;

void trinityStart();
void trinityStop();
void trinityProcess();
void TrinityTestShield();