// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"
#include "TrinityBuildEngine.h"

// ============================================
// Глобальное состояние плагина (управляется
// командами TSTART/TSTOP; доступ только из
// потока AutoCAD — таймер Windows ставится на
// UI-поток, поэтому гонок нет)
// ============================================
namespace {

const char* kDbHost  = "10.250.11.112";
const char* kDbUser  = "webdev";
const char* kDbPass  = "1QAZxsw2";
const char* kDbName  = "trinity_core";
const char* kCachePath = "D:\\trinity";
constexpr UINT_PTR kTimerIntervalMs = 5000;

TrinityBuildEngine* g_engine   = nullptr;
UINT_PTR            g_timerId  = 0;

// --------------------------------------------
// Остановка таймера и движка
// --------------------------------------------
void trinityStop() {
    if (g_timerId != 0) {
        KillTimer(NULL, g_timerId);
        g_timerId = 0;
    }

    if (g_engine) {
        delete g_engine;          // деструктор сам отключается от БД
        g_engine = nullptr;
    }

    acutPrintf(_T("\n[Trinity] Timer stopped.\n"));
}

// --------------------------------------------
// Обработка одного тика таймера
// --------------------------------------------
void trinityProcess() {
    if (!g_engine) return;

    AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();
    const int processed = g_engine->processAllProjects(db);

    if (processed > 0) acedUpdateDisplay();
}

VOID CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) {
    trinityProcess();
}

// --------------------------------------------
// TSTART — запуск таймера автосборки
// --------------------------------------------
void trinityStart() {
    if (g_timerId != 0) {
        acutPrintf(_T("\n[Trinity] Timer already running. Restarting...\n"));
        trinityStop();
    }

    g_engine = new(std::nothrow) TrinityBuildEngine(kCachePath);
    if (!g_engine) {
        acutPrintf(_T("\n[Trinity] Out of memory\n"));
        return;
    }

    if (!g_engine->init(kDbHost, kDbUser, kDbPass, kDbName)) {
        acutPrintf(_T("\n[Trinity] Failed to connect to database\n"));
        delete g_engine;
        g_engine = nullptr;
        return;
    }

    g_timerId = SetTimer(NULL, NULL, kTimerIntervalMs, TimerProc);
    if (g_timerId == 0) {
        acutPrintf(_T("\n[Trinity] Failed to create timer\n"));
        delete g_engine;
        g_engine = nullptr;
        return;
    }

    acutPrintf(_T("\n[Trinity] Timer started. Every %u seconds.\n"),
               kTimerIntervalMs / 1000);
}

// --------------------------------------------
// Регистрация команд
// --------------------------------------------
void registerCommand(const ACHAR* name, ACRX_CMD callback) {
    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"), name, name,
                            ACRX_CMD_MODAL, callback);
}

} // namespace

void trinityRegisterCommands() {
    registerCommand(_T("TSTART"), reinterpret_cast<ACRX_CMD>(&trinityStart));
    registerCommand(_T("TSTOP"),  reinterpret_cast<ACRX_CMD>(&trinityStop));
}

// Вызывается из acrxEntry при выгрузке приложения
void trinityShutdown() {
    trinityStop();
}
