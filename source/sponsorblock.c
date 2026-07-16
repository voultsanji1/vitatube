#include "sponsorblock.h"
#include "http.h"
#include "storage.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SponsorblockMode g_mode = SPONSORBLOCK_ADS;

SponsorblockMode getSponsorblockMode(void) { return g_mode; }
void setSponsorblockMode(SponsorblockMode mode) { g_mode = mode; }

static int hex4sb(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static int jsonStringSB(const char *from, const char *end, const char *key, char *out, int cap)
{
    char pat[48];
    int pl = snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = from ? strstr(from, pat) : NULL;
    if (!p || p >= end) { if (cap) out[0] = 0; return 0; }
    p += pl;
    int i = 0;
    while (p < end && *p && *p != '"' && i < cap - 1) {
        if (*p != '\\') { out[i++] = *p++; continue; }
        if (++p >= end) break;
        char c = *p++;
        if (c == 'u') {
            if (p + 4 > end) { p = end; break; }
            int v = hex4sb(p); p += 4;
            if (v >= 0 && v <= 0x7F) out[i++] = (char)v;
        } else out[i++] = c;
    }
    out[i] = 0;
    return 1;
}

static SponsorCategory categoryFromName(const char *name)
{
    if (!strcmp(name, "sponsor")) return SPONSOR_SPONSOR;
    if (!strcmp(name, "self-promotion")) return SPONSOR_SELFPROMO;
    if (!strcmp(name, "interaction")) return SPONSOR_INTERACTION;
    if (!strcmp(name, "intro")) return SPONSOR_INTRO;
    if (!strcmp(name, "outro")) return SPONSOR_OUTRO;
    if (!strcmp(name, "filler")) return SPONSOR_FILLER;
    if (!strcmp(name, "music_offtopic") || !strcmp(name, "poi_highlight")) return SPONSOR_MUSIC_OFFTOPIC;
    return SPONSOR_COUNT;
}

int fetchSponsorSegments(const char *videoId, SponsorSegments *out)
{
    out->count = 0;
    if (getSponsorblockMode() == SPONSORBLOCK_OFF)
        return 0;

    char url[256];
    snprintf(url, sizeof url,
             "https://sponsor.ajay.app/api/skipSegments?videoID=%s&category=sponsor&category=self-promotion&category=interaction&category=intro&category=outro&category=filler&category=music_offtopic",
             videoId);

    char *buf = malloc(512 * 1024);
    if (!buf)
        return -1;

    HttpHeader headers[] = { { "User-Agent", "VitaTube" } };
    int len = 0, status = 0;
    int rc = httpRequest("GET", url, headers, 1, NULL, 0, buf, 512 * 1024, &len, &status);
    if (rc != 0 || status != 200) {
        free(buf);
        return -1;
    }

    const char *p = buf;
    const char *end = buf + len;
    while (out->count < MAX_SEGMENTS) {
        const char *seg = strstr(p, "\"segment\"");
        if (!seg || seg >= end)
            break;
        const char *segEnd = strstr(seg + 8, "}]");
        if (!segEnd) segEnd = end;

        char startS[32], endS[32], cat[32];
        jsonStringSB(seg, segEnd, "start", startS, sizeof startS);
        jsonStringSB(seg, segEnd, "end", endS, sizeof endS);
        jsonStringSB(seg, segEnd, "category", cat, sizeof cat);

        SponsorCategory category = categoryFromName(cat);
        if (category >= SPONSOR_COUNT) {
            p = segEnd;
            continue;
        }
        SponsorSegment *s = &out->segments[out->count];
        s->start = (float)atof(startS);
        s->end = (float)atof(endS);
        s->category = category;
        out->count++;
        p = segEnd;
    }

    free(buf);
    return 0;
}
