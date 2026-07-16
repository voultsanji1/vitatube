#ifndef VITATUBE_CHAPTERS_H
#define VITATUBE_CHAPTERS_H

#define MAX_CHAPTERS 64

typedef struct {
    float start;
    char title[128];
} Chapter;

typedef struct {
    int count;
    Chapter chapters[MAX_CHAPTERS];
} ChapterList;

void parseChapters(const char *description, ChapterList *out);

#endif
