#ifndef VITATUBE_APP_H
#define VITATUBE_APP_H

#include "extractor.h"

int extractVideoInfo(const char *input, StreamInfo *out);
void playFromInput(const char *input);

void appRun(void);

#endif
