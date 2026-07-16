#include "subtitles.h"
#include "http.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void xmlUnescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; ) {
        if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; }
        else if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; }
        else if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; }
        else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"'; r += 6; }
        else if (strncmp(r, "&#39;", 5) == 0) { *w++ = '\''; r += 5; }
        else *w++ = *r++;
    }
    *w = 0;
}

int fetchSubtitles(const char *url, SubtitleTrack *out)
{
    out->count = 0;
    char *buf = malloc(2 * 1024 * 1024);
    if (!buf)
        return -1;

    HttpHeader headers[] = { { "User-Agent", "Mozilla/5.0" } };
    int len = 0, status = 0;
    int rc = httpRequest("GET", url, headers, 1, NULL, 0, buf, 2 * 1024 * 1024, &len, &status);
    if (rc != 0 || status != 200) {
        logError("subtitle fetch failed rc=%d status=%d\n", rc, status);
        free(buf);
        return -1;
    }

    const char *p = buf;
    const char *end = buf + len;
    while (out->count < MAX_SUBTITLE_CUES) {
        const char *textTag = strstr(p, "<text");
        if (!textTag || textTag >= end)
            break;
        const char *tagEnd = strchr(textTag, '>');
        if (!tagEnd)
            break;

        const char *start = NULL, *dur = NULL;
        const char *a = textTag;
        while (a < tagEnd) {
            if (strncmp(a, "start=\"", 7) == 0) { start = a + 7; }
            else if (strncmp(a, "dur=\"", 5) == 0) { dur = a + 5; }
            a++;
        }

        const char *textStart = tagEnd + 1;
        const char *textEnd = strstr(textStart, "</text>");
        if (!textEnd)
            break;

        SubtitleCue *cue = &out->cues[out->count];
        cue->start = start ? (float)atof(start) : 0.0f;
        cue->end = dur ? cue->start + (float)atof(dur) : cue->start + 3.0f;

        int tlen = (int)(textEnd - textStart);
        if (tlen >= 256) tlen = 255;
        memcpy(cue->text, textStart, tlen);
        cue->text[tlen] = 0;
        xmlUnescape(cue->text);

        out->count++;
        p = textEnd + 7;
    }

    free(buf);
    return 0;
}

const char *getSubtitleCueText(SubtitleTrack *track, float pos, int *scanFrom)
{
    if (!track || track->count == 0)
        return NULL;

    if (*scanFrom >= track->count || *scanFrom < 0)
        *scanFrom = 0;

    for (int i = *scanFrom; i < track->count; i++) {
        if (pos >= track->cues[i].start && pos < track->cues[i].end) {
            *scanFrom = i;
            return track->cues[i].text;
        }
        if (track->cues[i].start > pos) {
            *scanFrom = i > 0 ? i - 1 : 0;
            return NULL;
        }
    }
    *scanFrom = track->count - 1;
    return NULL;
}
