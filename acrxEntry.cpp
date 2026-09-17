// acrxEntry.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"

// ============================================
// РЕГИСТРАЦИЯ КОМАНД
// ============================================
void initApp() {
    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"),
        _T("TSTART"), _T("TSTART"),
        ACRX_CMD_MODAL, trinityStart);

    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"),
        _T("TSTOP"), _T("TSTOP"),
        ACRX_CMD_MODAL, trinityStop);

    // Тестовая команда для динамических щитов
    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"),
        _T("TEST_SHIELD"), _T("TEST_SHIELD"),
        ACRX_CMD_MODAL, TrinityTestShield);

    acutPrintf(_T("\n[Trinity] Plugin loaded. Commands: TSTART, TSTOP, TEST_SHIELD\n"));
}

// ============================================
// ВЫГРУЗКА
// ============================================
void unloadApp() {
    trinityStop();
    acedRegCmds->removeGroup(_T("TRINITY_COMMANDS"));
}

// ============================================
// ТОЧКА ВХОДА AutoCAD
// ============================================
extern "C"
AcRx::AppRetCode acrxEntryPoint(AcRx::AppMsgCode msg, void* appId) {
    switch (msg) {
        case AcRx::kInitAppMsg:
            acrxDynamicLinker->unlockApplication(appId);
            acrxDynamicLinker->registerAppMDIAware(appId);
            initApp();
            break;
        case AcRx::kUnloadAppMsg:
            unloadApp();
            break;
    }
    return AcRx::kRetOK;
}