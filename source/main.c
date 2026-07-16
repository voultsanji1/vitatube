#include "ui.h"
#include "app.h"
#include "log.h"
#include "http.h"
#include "storage.h"
#include "player.h"
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <vitaGL.h>
#include <stdio.h>

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    scePowerSetArmClockFrequency(444);

    SceAppUtilInitParam appUtilInit;
    SceAppUtilBootParam appUtilBoot;
    memset(&appUtilInit, 0, sizeof appUtilInit);
    memset(&appUtilBoot, 0, sizeof appUtilBoot);
    sceAppUtilInit(&appUtilInit, &appUtilBoot);

    SceCommonDialogConfigParam commonCfg;
    sceCommonDialogConfigParamInit(&commonCfg);
    sceCommonDialogSetConfigParam(&commonCfg);

    vglInitExtended(0, 960, 544, 4 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);

    logInit();
    storageInit();
    httpInit();
    uiInit();
    playerInit();

    logInfo("VitaTube starting\n");
    appRun();

    playerTerm();
    uiEndFrame();
    httpTerm();
    vglEnd();
    return 0;
}
