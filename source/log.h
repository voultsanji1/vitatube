#ifndef VITATUBE_LOG_H
#define VITATUBE_LOG_H

void logInit(void);
void logInfo(const char *fmt, ...);
void logError(const char *fmt, ...);
void logWarn(const char *fmt, ...);

#endif
