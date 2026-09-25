// acrxEntry.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"

// ============================================
// РЕГИСТРАЦИЯ КОМАНД
// ============================================
void initApp() {
    trinityRegisterCommands();
    acutPrintf(_T("\n[Trinity] Plugin loaded. TRINITYTEST / TRINITYBUILD / TRINITYLOAD\n"));
}

// ============================================
// ВЫГРУЗКА
// ============================================
void unloadApp() {
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