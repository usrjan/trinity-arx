// acrxEntry.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"

void initApp() {
    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"), _T("TRINITY_START"), _T("TRINITY_START"), ACRX_CMD_MODAL, trinityStart);
    acedRegCmds->addCommand(_T("TRINITY_COMMANDS"), _T("TRINITY_STOP"), _T("TRINITY_STOP"), ACRX_CMD_MODAL, trinityStop);
    acutPrintf(_T("\n[Trinity] Plugin loaded. TRINITY_START / TRINITY_STOP\n"));
}

void unloadApp() {
    trinityStop();
    acedRegCmds->removeGroup(_T("TRINITY_COMMANDS"));
}

extern "C" AcRx::AppRetCode acrxEntryPoint(AcRx::AppMsgCode msg, void* appId) {
    switch(msg) {
        case AcRx::kInitAppMsg: acrxDynamicLinker->unlockApplication(appId); acrxDynamicLinker->registerAppMDIAware(appId); initApp(); break;
        case AcRx::kUnloadAppMsg: unloadApp(); break;
    }
    return AcRx::kRetOK;
}