#include "stream_select.h"
#include <string.h>

static int decodableVideo(const StreamFormat *format)
{
    if (format->height <= 720 && format->fps <= 60)
        return 1;
    return 0;
}

const StreamFormat *pickBestVideo(const StreamInfo *info)
{
    const StreamFormat *best = NULL;
    for (int i = 0; i < info->formatCount; i++) {
        const StreamFormat *format = &info->formats[i];
        if (!format->hasVideo || format->needsCipher || !format->url[0])
            continue;
        if (strcmp(format->container, "mp4") != 0)
            continue;
        if (!decodableVideo(format))
            continue;
        if (!best || format->height > best->height ||
            (format->height == best->height && format->fps > best->fps))
            best = format;
    }
    return best;
}

const StreamFormat *pickBestAudio(const StreamInfo *info)
{
    const StreamFormat *best = NULL;
    for (int i = 0; i < info->formatCount; i++) {
        const StreamFormat *format = &info->formats[i];
        if (!format->hasAudio || format->hasVideo || format->needsCipher || !format->url[0])
            continue;
        if (strcmp(format->container, "mp4") != 0)
            continue;
        if (!best || format->itag == 140)
            best = format;
    }
    return best;
}
