#ifndef VITATUBE_HTTP_H
#define VITATUBE_HTTP_H

#include <stdint.h>

typedef struct {
    const char *name;
    const char *value;
} HttpHeader;

int httpInit(void);
void httpTerm(void);

int httpRequest(const char *method, const char *url,
                const HttpHeader *headers, int headerCount,
                const char *body, int bodyLen,
                char *out, int outCap, int *outLen, int *status);

typedef struct {
    void *internal;
    const char *url;
    int status;
    int error;
} HttpStream;

HttpStream *httpStreamOpen(const char *url, const HttpHeader *headers, int headerCount);
int httpStreamRead(HttpStream *s, uint64_t offset, char *buf, uint32_t len, uint32_t *read);
uint64_t httpStreamSize(HttpStream *s);
void httpStreamClose(HttpStream *s);

#endif
