// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"

TrinityBuildEngine* g_engine = nullptr;
UINT_PTR g_timerId = 0;

void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) { trinityProcess(); }

void trinityStart() {
    if(g_timerId != 0) { acutPrintf(_T("\n[Trinity] Timer already running\n")); return; }
    if(!g_engine) {
        g_engine = new TrinityBuildEngine("D:\\trinity");
        if(!g_engine->init("10.250.11.112", "webdev", "1QAZxsw2", "trinity_core")) {
            acutPrintf(_T("\n[Trinity] Failed to connect\n"));
            delete g_engine; g_engine = nullptr; return;
        }
    }
    g_timerId = SetTimer(NULL, NULL, 10000, TimerProc);
    acutPrintf(_T("\n[Trinity] Timer started. Every 10 seconds.\n"));
}

void trinityStop() {
    if(g_timerId == 0) { acutPrintf(_T("\n[Trinity] Timer not running\n")); return; }
    KillTimer(NULL, g_timerId); g_timerId = 0;
    if(g_engine) { g_engine->shutdown(); delete g_engine; g_engine = nullptr; }
    acutPrintf(_T("\n[Trinity] Timer stopped.\n"));
}

void trinityProcess() {
    if(!g_engine) return;
    AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();
    int processed = g_engine->processAllProjects(db);
    if(processed > 0) acedUpdateDisplay();
}