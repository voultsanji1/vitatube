#include "storage.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <string.h>
#include <stdlib.h>

int storageInit(void)
{
    sceIoMkdir(DATA_DIR, 0777);
    return 0;
}

int fileRead(const char *path, char *buf, int cap)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return -1;
    int total = 0;
    int r;
    while (total < cap - 1 && (r = sceIoRead(fd, buf + total, cap - 1 - total)) > 0)
        total += r;
    buf[total] = 0;
    sceIoClose(fd);
    return total;
}

int fileWrite(const char *path, const char *buf, int len)
{
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return -1;
    int written = sceIoWrite(fd, (void *)buf, len);
    sceIoClose(fd);
    return written;
}

int fileExists(const char *path)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    sceIoClose(fd);
    return 1;
}

void fileDelete(const char *path)
{
    sceIoRemove(path);
}
