#include "http.h"
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36"

static int g_template = -1;
static int g_initialized = 0;

int httpInit(void)
{
    if (g_initialized)
        return 0;

    static SceNetInitParam netParam;
    memset(&netParam, 0, sizeof netParam);
    int ret = sceNetInit(&netParam);
    if (ret < 0 && ret != 0x80412101)
        return ret;
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    if (sceHttpInit(1 * 1024 * 1024) < 0)
        return -1;

    g_template = sceHttpCreateTemplate(USER_AGENT, SCE_HTTP_VERSION_1_1, 1);
    if (g_template < 0)
        return g_template;

    g_initialized = 1;
    return 0;
}

void httpTerm(void)
{
    if (!g_initialized)
        return;
    if (g_template >= 0)
        sceHttpDeleteTemplate(g_template);
    sceHttpTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
    sceNetTerm();
    g_initialized = 0;
    g_template = -1;
}

int httpRequest(const char *method, const char *url,
                const HttpHeader *headers, int headerCount,
                const char *body, int bodyLen,
                char *out, int outCap, int *outLen, int *status)
{
    int conn = sceHttpCreateConnectionWithURL(g_template, url, SCE_TRUE);
    if (conn < 0)
        return conn;

    int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_POST, url, 0);
    if (req < 0) {
        sceHttpDeleteConnection(conn);
        return req;
    }

    sceHttpSetAutoRedirect(req, SCE_TRUE);

    for (int i = 0; i < headerCount; i++)
        sceHttpAddRequestHeader(req, headers[i].name, headers[i].value, SCE_HTTP_HEADER_ADD);

    sceHttpAddRequestHeader(req, "Accept-Encoding", "identity", SCE_HTTP_HEADER_ADD);

    int rc = sceHttpSendRequest(req, body, bodyLen);

    int st = 0;
    if (rc >= 0) {
        if (sceHttpGetStatusCode(req, &st) == 0)
            *status = st;
        else
            *status = 0;
    } else {
        *status = 0;
    }

    int total = 0;
    if (rc >= 0) {
        unsigned char buf[16 * 1024];
        int r;
        while ((r = sceHttpReadData(req, buf, sizeof(buf))) > 0) {
            if (total + r > outCap)
                r = outCap - total;
            if (r <= 0)
                break;
            memcpy(out + total, buf, r);
            total += r;
            if (total >= outCap)
                break;
        }
    }

    *outLen = total;
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    return rc;
}

typedef struct {
    const char *url;
    int status;
    int error;
} HttpStreamInternal;

HttpStream *httpStreamOpen(const char *url, const HttpHeader *headers, int headerCount)
{
    (void)headers; (void)headerCount;
    HttpStreamInternal *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->url = url;

    HttpStream *out = calloc(1, sizeof(*out));
    out->internal = s;
    out->url = url;
    out->status = 0;
    out->error = 0;
    return out;
}

static int streamReadOnce(const char *url, uint64_t offset, char *buf, uint32_t len, uint32_t *read)
{
    int conn = sceHttpCreateConnectionWithURL(g_template, url, SCE_TRUE);
    if (conn < 0) {
        *read = 0;
        return conn;
    }
    int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
    if (req < 0) {
        sceHttpDeleteConnection(conn);
        *read = 0;
        return req;
    }

    char range[64];
    snprintf(range, sizeof(range), "bytes=%llu-%llu", (unsigned long long)offset,
             (unsigned long long)(offset + len - 1));
    sceHttpAddRequestHeader(req, "Range", range, SCE_HTTP_HEADER_ADD);

    int rc = sceHttpSendRequest(req, NULL, 0);
    int got = 0;
    if (rc >= 0) {
        int r;
        while (got < (int)len && (r = sceHttpReadData(req, buf + got, len - got)) > 0)
            got += r;
    }

    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);

    if (rc < 0) {
        *read = 0;
        return rc;
    }
    *read = (uint32_t)got;
    return 0;
}

int httpStreamRead(HttpStream *s, uint64_t offset, char *buf, uint32_t len, uint32_t *read)
{
    HttpStreamInternal *in = s->internal;
    if (!in) {
        *read = 0;
        return -1;
    }
    return streamReadOnce(in->url, offset, buf, len, read);
}

uint64_t httpStreamSize(HttpStream *s)
{
    (void)s;
    return 0;
}

void httpStreamClose(HttpStream *s)
{
    if (!s)
        return;
    free(s->internal);
    free(s);
}
