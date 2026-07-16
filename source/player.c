#include "player.h"
#include "http.h"
#include "log.h"
#include <vita2d.h>
#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/display.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_W 960
#define SCREEN_H 544

typedef struct {
    HttpStream *stream;
    char url[MAX_STREAM_URL];
} AvSource;

static AvSource g_source;
static SceAvPlayerHandle g_handle = 0;
static PlayerState g_state = PLAYER_IDLE;
static char g_message[128];
static int g_width = 0, g_height = 0, g_duration = 0, g_paused = 0;
static float g_volume = 1.0f;

static void *avAlloc(void *arg, uint32_t alignment, uint32_t size)
{
    (void)arg; (void)alignment;
    return malloc(size);
}

static void avFree(void *arg, void *ptr)
{
    (void)arg;
    free(ptr);
}

static void *avAllocFrame(void *arg, uint32_t alignment, uint32_t size)
{
    (void)arg; (void)alignment;
    return malloc(size);
}

static void avFreeFrame(void *arg, void *ptr)
{
    (void)arg;
    free(ptr);
}

static int avOpenFile(void *p, const char *filename)
{
    (void)filename;
    AvSource *src = p;
    src->stream = httpStreamOpen(src->url, NULL, 0);
    if (!src->stream)
        return -1;
    return 0;
}

static int avCloseFile(void *p)
{
    AvSource *src = p;
    if (src->stream) {
        httpStreamClose(src->stream);
        src->stream = NULL;
    }
    return 0;
}

static int avReadOffset(void *p, uint8_t *buffer, uint64_t position, uint32_t length)
{
    AvSource *src = p;
    if (!src->stream)
        return -1;
    uint32_t read = 0;
    if (httpStreamRead(src->stream, position, (char *)buffer, length, &read) != 0)
        return -1;
    return (int)read;
}

static uint64_t avSizeFile(void *p)
{
    (void)p;
    return 1ULL << 40;
}

int playerInit(void)
{
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    return 0;
}

void playerTerm(void)
{
    playerClose();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
}

int playerOpen(const StreamFormat *video, const StreamFormat *audio)
{
    playerClose();

    const char *url = video->url;
    snprintf(g_source.url, sizeof g_source.url, "%s", url);

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof init);
    init.memoryReplacement.objectPointer = NULL;
    init.memoryReplacement.allocate = avAlloc;
    init.memoryReplacement.deallocate = avFree;
    init.memoryReplacement.allocateTexture = avAllocFrame;
    init.memoryReplacement.deallocateTexture = avFreeFrame;
    init.fileReplacement.objectPointer = &g_source;
    init.fileReplacement.open = avOpenFile;
    init.fileReplacement.close = avCloseFile;
    init.fileReplacement.readOffset = avReadOffset;
    init.fileReplacement.size = avSizeFile;
    init.basePriority = 0x10000100;
    init.numOutputVideoFrameBuffers = 4;
    init.autoStart = SCE_TRUE;

    g_handle = sceAvPlayerInit(&init);
    if (!g_handle) {
        strncpy(g_message, "Failed to init player", sizeof g_message);
        g_state = PLAYER_FAILED;
        return -1;
    }

    if (sceAvPlayerAddSource(g_handle, g_source.url) != 0) {
        strncpy(g_message, "Failed to add source", sizeof g_message);
        g_state = PLAYER_FAILED;
        return -1;
    }

    g_state = PLAYER_LOADING;
    g_width = video->width;
    g_height = video->height;
    (void)audio;
    return 0;
}

void playerClose(void)
{
    if (g_handle) {
        sceAvPlayerStop(g_handle);
        sceAvPlayerClose(g_handle);
        g_handle = 0;
    }
    if (g_source.stream) {
        httpStreamClose(g_source.stream);
        g_source.stream = NULL;
    }
    g_state = PLAYER_IDLE;
}

void playerUpdate(PlayerStatus *status)
{
    if (!g_handle) {
        status->state = g_state;
        strncpy(status->message, g_message, sizeof status->message);
        return;
    }

    if (g_state == PLAYER_LOADING) {
        uint32_t count = sceAvPlayerStreamCount(g_handle);
        for (uint32_t i = 0; i < count; i++) {
            SceAvPlayerStreamInfo info;
            memset(&info, 0, sizeof info);
            if (sceAvPlayerGetStreamInfo(g_handle, i, &info) == 0) {
                if (info.type == SCE_AVPLAYER_VIDEO && info.details.video.width > 0) {
                    g_width = info.details.video.width;
                    g_height = info.details.video.height;
                    g_duration = (int)(info.duration / 1000);
                    g_state = PLAYER_PLAYING;
                }
            }
        }
    }

    status->state = g_state;
    status->width = g_width;
    status->height = g_height;
    status->duration = g_duration;
    status->paused = g_paused;

    if (g_state == PLAYER_PLAYING) {
        uint64_t t = sceAvPlayerCurrentTime(g_handle);
        status->position = (int)(t / 1000);
    }

    strncpy(status->message, g_message, sizeof status->message);
}

void playerDraw(void)
{
    if (!g_handle || g_state != PLAYER_PLAYING)
        return;

    SceAvPlayerFrameInfo info;
    memset(&info, 0, sizeof info);
    if (sceAvPlayerGetVideoData(g_handle, &info) == SCE_FALSE || !info.pData)
        return;

    int w = g_width;
    int h = g_height;
    if (w <= 0 || h <= 0 || w > 1920 || h > 1080)
        return;

    vita2d_texture *tex = vita2d_create_empty_texture_format(w, h,
        SCE_GXM_TEXTURE_FORMAT_R5G6B5);
    if (!tex)
        return;

    uint16_t *dst = vita2d_texture_get_datap(tex);
    const uint8_t *y = info.pData;
    const uint8_t *u = y + (size_t)w * h;
    const uint8_t *v = u + (size_t)w * h / 4;
    int pitch = w;

    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int yv = y[yy * pitch + xx];
            int uv = u[(yy / 2) * (pitch / 2) + (xx / 2)];
            int vv = v[(yy / 2) * (pitch / 2) + (xx / 2)];
            int cr = yv + ((vv - 128) * 1436 >> 10);
            int cg = yv - (((uv - 128) * 352 + (vv - 128) * 731) >> 10);
            int cb = yv + ((uv - 128) * 1814 >> 10);
            if (cr < 0) cr = 0; else if (cr > 255) cr = 255;
            if (cg < 0) cg = 0; else if (cg > 255) cg = 255;
            if (cb < 0) cb = 0; else if (cb > 255) cb = 255;
            dst[yy * w + xx] = (uint16_t)(((cr >> 3) << 11) | ((cg >> 2) << 5) | (cb >> 3));
        }
    }

    int dw = SCREEN_W;
    float scale = (float)SCREEN_H / h;
    dw = (int)(w * scale);
    int dx = (SCREEN_W - dw) / 2;
    vita2d_draw_texture_scale(tex, dx, 0, (float)dw / w, (float)SCREEN_H / h);
    vita2d_free_texture(tex);
}

void playerTogglePause(void)
{
    if (!g_handle)
        return;
    if (g_paused) {
        sceAvPlayerResume(g_handle);
        g_paused = 0;
    } else {
        sceAvPlayerPause(g_handle);
        g_paused = 1;
    }
}

void playerSeek(float seconds)
{
    if (!g_handle)
        return;
    uint64_t t = (uint64_t)(seconds * 1000);
    sceAvPlayerJumpToTime(g_handle, t);
}

void playerSetVolume(float fraction)
{
    (void)fraction;
    g_volume = fraction;
}
