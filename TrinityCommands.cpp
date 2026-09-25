// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"

TrinityBuildEngine* g_engine = nullptr;

// ============================================
// СОЗДАНИЕ ДВИЖКА И ПОДКЛЮЧЕНИЕ К БАЗЕ
// ============================================
static bool ensureEngine() {
    if (g_engine) return true;

    g_engine = new TrinityBuildEngine("D:\\trinity");

    // Параметры подключения — источник истины: база trinity_core
    if (!g_engine->init("10.250.11.112", "webdev", "1QAZxsw2", "trinity_core")) {
        acutPrintf(_T("\n[Trinity] Failed to connect to database\n"));
        delete g_engine;
        g_engine = nullptr;
        return false;
    }

    return true;
}

// ============================================
// TSTART — инициализация движка
// ============================================
void trinityStart() {
    // Принудительная очистка старого движка перед созданием нового
    // (идемпотентность повторного вызова)
    if (g_engine) {
        delete g_engine;
        g_engine = nullptr;
    }

    if (ensureEngine()) {
        acutPrintf(_T("\n[Trinity] Engine started. Use TPROCESS to build pending projects.\n"));
    }
}

// ============================================
// TSTOP — остановка и очистка
// ============================================
void trinityStop() {
    if (g_engine) {
        g_engine->shutdown();
        delete g_engine;
        g_engine = nullptr;
    }

    acutPrintf(_T("\n[Trinity] Engine stopped.\n"));
}

// ============================================
// TRINITY_PROCESS — одна обработка pending-проектов
// ============================================
// Вызывается как команда из командной строки AutoCAD, а НЕ из
// колбэка таймера: сторонний код не должен выполняться в момент,
// когда пользователь запускает модальную команду или AutoCAD
// загружает/выгружает документ (это приводило к падению).
void trinityProcess() {
    if (!ensureEngine()) return;

    AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();
    if (!db) {
        acutPrintf(_T("\n[Trinity] No working database.\n"));
        return;
    }

    int processed = g_engine->processAllProjects(db);

    if (processed > 0) {
        acedUpdateDisplay();
        acutPrintf(_T("\n[Trinity] Processed %d project(s).\n"), processed);
    } else {
        acutPrintf(_T("\n[Trinity] No pending projects.\n"));
    }
}
