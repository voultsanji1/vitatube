#include "ui.h"
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <string.h>
#include <stdio.h>

static vita2d_pgf *g_font;

void uiInit(void)
{
    vita2d_init();
    vita2d_set_clear_color(COLOR_BG);
    g_font = vita2d_load_default_pgf();
}

void uiBeginFrame(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void uiEndFrame(void)
{
    vita2d_end_drawing();
    vita2d_swap_buffers();
    sceDisplayWaitVblankStart();
}

void uiText(int x, int y, const char *text, unsigned int color)
{
    if (!g_font || !text)
        return;
    vita2d_pgf_draw_text(g_font, x, y, color, 1.0f, text);
}

int uiTextWidth(const char *text)
{
    if (!g_font || !text)
        return 0;
    return (int)vita2d_pgf_text_width(g_font, 1.0f, text);
}

void uiRect(int x, int y, int w, int h, unsigned int color)
{
    vita2d_draw_rectangle(x, y, w, h, color);
}

void uiReadPad(SceCtrlData *pad, SceCtrlData *padPrev)
{
    *padPrev = *pad;
    sceCtrlPeekBufferPositive(0, pad, 1);
}

int uiPressed(SceCtrlData *pad, SceCtrlData *prev, uint32_t btn)
{
    return (pad->buttons & btn) && !(prev->buttons & btn);
}
