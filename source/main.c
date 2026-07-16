#include "ui.h"
#include "app.h"
#include "log.h"
#include "http.h"
#include "storage.h"
#include "player.h"
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <stdio.h>

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    scePowerSetArmClockFrequency(444);

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
    return 0;
}
