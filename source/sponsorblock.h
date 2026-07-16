#ifndef VITATUBE_SPONSORBLOCK_H
#define VITATUBE_SPONSORBLOCK_H

typedef enum {
    SPONSOR_SPONSOR,
    SPONSOR_SELFPROMO,
    SPONSOR_INTERACTION,
    SPONSOR_INTRO,
    SPONSOR_OUTRO,
    SPONSOR_FILLER,
    SPONSOR_MUSIC_OFFTOPIC,
    SPONSOR_COUNT
} SponsorCategory;

typedef struct {
    float start;
    float end;
    SponsorCategory category;
} SponsorSegment;

#define MAX_SEGMENTS 64

typedef struct {
    int count;
    SponsorSegment segments[MAX_SEGMENTS];
} SponsorSegments;

typedef enum {
    SPONSORBLOCK_OFF,
    SPONSORBLOCK_ADS,
    SPONSORBLOCK_ALL
} SponsorblockMode;

SponsorblockMode getSponsorblockMode(void);
void setSponsorblockMode(SponsorblockMode mode);

int fetchSponsorSegments(const char *videoId, SponsorSegments *out);

#endif
