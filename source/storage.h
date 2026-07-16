#ifndef VITATUBE_STORAGE_H
#define VITATUBE_STORAGE_H

#define DATA_DIR "ux0:data/vitatube"

int storageInit(void);

int fileRead(const char *path, char *buf, int cap);
int fileWrite(const char *path, const char *buf, int len);
int fileExists(const char *path);
void fileDelete(const char *path);

#endif
