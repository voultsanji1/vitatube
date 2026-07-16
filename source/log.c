#include "log.h"
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_PATH "ux0:data/vitatube/log.txt"

void logInit(void)
{
    sceIoRemove(LOG_PATH);
}

static void vlog(const char *prefix, const char *fmt, va_list ap)
{
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[VitaTube] %s ", prefix);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    sceClibPrintf("%s", buf);

    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, (unsigned int)strlen(buf));
        sceIoClose(fd);
    }
}

void logInfo(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog("INFO", fmt, ap); va_end(ap); }
void logError(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog("ERR ", fmt, ap); va_end(ap); }
void logWarn(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog("WARN", fmt, ap); va_end(ap); }
