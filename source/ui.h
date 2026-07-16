#ifndef VITATUBE_UI_H
#define VITATUBE_UI_H

#include <psp2/ctrl.h>

void uiInit(void);
void uiBeginFrame(void);
void uiEndFrame(void);

void uiText(int x, int y, const char *text, unsigned int color);
int uiTextWidth(const char *text);
void uiRect(int x, int y, int w, int h, unsigned int color);

void uiReadPad(SceCtrlData *pad, SceCtrlData *padPrev);
int uiPressed(SceCtrlData *pad, SceCtrlData *prev, uint32_t btn);

#define COLOR_BG      0xFF101418
#define COLOR_FG      0xFFE0E0E0
#define COLOR_ACCENT  0xFF0000FF
#define COLOR_DIM     0xFF808080

#endif
