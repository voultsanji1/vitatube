#include "chapters.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void parseChapters(const char *description, ChapterList *out)
{
    out->count = 0;
    if (!description || !description[0])
        return;

    const char *p = description;
    while (*p && out->count < MAX_CHAPTERS) {
        const char *line = p;
        const char *nl = strchr(p, '\n');
        int lineLen = nl ? (int)(nl - p) : (int)strlen(p);

        int h = 0, m = 0, s = 0;
        const char *scan = line;
        if (sscanf(scan, "%d:%d:%d", &h, &m, &s) == 3) {
        } else if (sscanf(scan, "%d:%d", &m, &s) == 2) {
            h = 0;
        } else {
            p = nl ? nl + 1 : p + lineLen;
            continue;
        }

        const char *title = scan;
        while (*title && *title != ' ') title++;
        while (*title == ' ') title++;

        Chapter *ch = &out->chapters[out->count];
        ch->start = (float)(h * 3600 + m * 60 + s);
        if ((int)strlen(title) > 0 && (int)strlen(title) < 128)
            strncpy(ch->title, title, 127);
        else
            strncpy(ch->title, "(chapter)", 127);
        ch->title[127] = 0;
        out->count++;

        p = nl ? nl + 1 : p + lineLen;
    }
}
