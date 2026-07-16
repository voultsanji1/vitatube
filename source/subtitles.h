#ifndef VITATUBE_SUBTITLES_H
#define VITATUBE_SUBTITLES_H

#define MAX_SUBTITLE_CUES 512

typedef struct {
    float start;
    float end;
    char text[256];
} SubtitleCue;

typedef struct {
    int count;
    SubtitleCue cues[MAX_SUBTITLE_CUES];
} SubtitleTrack;

int fetchSubtitles(const char *url, SubtitleTrack *out);

const char *getSubtitleCueText(SubtitleTrack *track, float pos, int *scanFrom);

#endif
