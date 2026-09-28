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

    // TRIB — команда этапа разработки: нарисовать в текущем чертеже
    // все планки из базы (category='rib')
    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"),
        _T("TRIB"), _T("TRIB"),
        ACRX_CMD_MODAL, trinityTrib);

    acutPrintf(_T("\n[Trinity] Plugin loaded. TSTART / TSTOP / TRIB\n"));
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