#ifndef VITATUBE_COMMON_H
#define VITATUBE_COMMON_H

#define MAX_STREAM_URL 2048
#define MAX_STREAM_FORMATS 24

typedef struct {
    int itag;
    int width;
    int height;
    int fps;
    int hasVideo;
    int hasAudio;
    int needsCipher;
    char container[8];
    char url[MAX_STREAM_URL];
    int isLiveSegmented;
    long liveStartSq;
    long liveEdgeSq;
} StreamFormat;

#define MAX_CAPTION_TRACKS 8

typedef struct {
    char languageCode[12];
    char name[48];
    char url[MAX_STREAM_URL];
} CaptionTrack;

typedef struct {
    char title[256];
    char description[2048];
    int captionCount;
    CaptionTrack captions[MAX_CAPTION_TRACKS];
    int durationSeconds;
    int isLive;
    int formatCount;
    StreamFormat formats[MAX_STREAM_FORMATS];
} StreamInfo;

#define MAX_SEARCH_RESULTS 200

typedef struct {
    char videoId[16];
    char channelId[32];
    char title[160];
    char duration[12];
    char author[48];
    char views[24];
    char published[24];
    int isLive;
} SearchResult;

#define MAX_CONTINUATION 4096
#define MAX_SORT_TOKEN 512

typedef struct {
    int count;
    char continuation[MAX_CONTINUATION];
    char channelName[48];
    char sortTokens[3][MAX_SORT_TOKEN];
    SearchResult items[MAX_SEARCH_RESULTS];
} SearchResults;

typedef enum {
    TREND_GAMING,
    TREND_SPORTS,
    TREND_PODCASTS,
    TREND_COUNT
} TrendingCategory;

typedef enum {
    SORT_RELEVANCE,
    SORT_VIEWS,
    SORT_COUNT
} SortOrder;

typedef enum {
    CHANNEL_SORT_LATEST,
    CHANNEL_SORT_POPULAR,
    CHANNEL_SORT_OLDEST,
    CHANNEL_SORT_COUNT
} ChannelSort;

typedef struct Extractor {
    const char *name;
    int (*matches)(const char *input);
    int (*extract)(const char *input, StreamInfo *out);
    int (*search)(const char *query, SortOrder sort, const char *continuation, SearchResults *out);
    int (*getTrending)(TrendingCategory category, const char *continuation, SearchResults *out);
    int (*getChannel)(const char *channelId, const char *token, SearchResults *out);
} Extractor;

const Extractor *findExtractor(const char *input);
int searchVideos(const char *query, SortOrder sort, const char *continuation, SearchResults *out);
int getTrending(TrendingCategory category, const char *continuation, SearchResults *out);
int getChannelVideos(const char *channelId, const char *token, SearchResults *out);

#endif
