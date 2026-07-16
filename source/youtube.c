#include "youtube.h"
#include "http.h"
#include "storage.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PLAYER_URL "https://www.youtube.com/youtubei/v1/player?prettyPrint=false"
#define SEARCH_URL "https://www.youtube.com/youtubei/v1/search?prettyPrint=false"
#define BROWSE_URL "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false"
#define RESP_CAP (2 * 1024 * 1024)

#define VR_VERSION "1.65.10"
#define WEB_VERSION "2.20250701.00.00"

#define VISITOR_URL "https://www.youtube.com/youtubei/v1/visitor_id?key=AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8&prettyPrint=false"

#define VR_CLIENT \
    "\"clientName\":\"ANDROID_VR\",\"clientVersion\":\"" VR_VERSION "\"," \
    "\"deviceMake\":\"Oculus\",\"deviceModel\":\"Quest 3\",\"androidSdkVersion\":32," \
    "\"osName\":\"Android\",\"osVersion\":\"12L\",\"hl\":\"en\",\"gl\":\"US\"," \
    "\"timeZone\":\"UTC\",\"utcOffsetMinutes\":0"
#define WEB_CONTEXT \
    "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"" WEB_VERSION "\",\"hl\":\"en\",\"gl\":\"US\"}}"

static const char *VR_UA =
    "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip";
static const char *WEB_UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36";

static const char *PLAYER_BODY_FMT =
    "{\"context\":{\"client\":{" VR_CLIENT ",\"visitorData\":\"%s\"}},\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}";
static const char *VISITOR_BODY = WEB_CONTEXT "}";
static const char *SEARCH_SORT_PARAMS[SORT_COUNT] = {
    "EgIQAQ==",
    "CAMSAhAB",
};
static const char *SEARCH_BODY_FMT = WEB_CONTEXT ",\"query\":\"%s\",\"params\":\"%s\"}";
static const char *TRENDING_BODY_FMT = WEB_CONTEXT ",\"browseId\":\"%s\",\"params\":\"%s\"}";
static const char *CONTINUATION_BODY_FMT = WEB_CONTEXT ",\"continuation\":\"%s\"}";

static int hex4(const char *p)
{
    int value = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return -1;
        value = value * 16 + digit;
    }
    return value;
}

static int jsonString(const char *from, const char *end, const char *key, char *out, int cap)
{
    char pat[48];
    int patLen = snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = from ? strstr(from, pat) : NULL;
    if (!p || p >= end) { if (cap) out[0] = 0; return 0; }
    p += patLen;

    int i = 0;
    while (p < end && *p && *p != '"' && i < cap - 1) {
        if (*p != '\\') { out[i++] = *p++; continue; }
        if (++p >= end) break;
        char c = *p++;
        switch (c) {
            case 'n': out[i++] = '\n'; break;
            case 't': out[i++] = '\t'; break;
            case 'r': out[i++] = '\r'; break;
            case 'b': out[i++] = '\b'; break;
            case 'f': out[i++] = '\f'; break;
            case 'u': {
                if (p + 4 > end) { p = end; break; }
                int value = hex4(p);
                p += 4;
                if (value >= 0 && value <= 0x7F) out[i++] = (char)value;
                break;
            }
            default: out[i++] = c; break;
        }
    }
    out[i] = 0;
    return 1;
}

static int jsonInt(const char *from, const char *end, const char *key, int *out)
{
    char pat[48];
    int patLen = snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = from ? strstr(from, pat) : NULL;
    if (!p || p >= end) return 0;
    p += patLen;
    if (*p == '"') p++;
    *out = atoi(p);
    return 1;
}

static int matches(const char *input)
{
    if (!input) return 0;
    if (strstr(input, "youtu")) return 1;
    return (int)strlen(input) == 11;
}

static int isIdChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static int extractVideoId(const char *input, char *out, int cap)
{
    static const char *markers[] = { "v=", "youtu.be/", "/shorts/", "/embed/", "/live/" };
    const char *id = NULL;
    for (unsigned i = 0; i < sizeof markers / sizeof markers[0]; i++) {
        const char *hit = strstr(input, markers[i]);
        if (hit) { id = hit + strlen(markers[i]); break; }
    }
    if (!id) id = ((int)strlen(input) == 11) ? input : NULL;
    if (!id) return 0;

    int length = 0;
    while (length < 11 && length < cap - 1 && isIdChar(id[length])) { out[length] = id[length]; length++; }
    out[length] = 0;
    return length == 11;
}

static void parseFormatArray(const char *arrayStart, const char *regionEnd, int progressive, StreamInfo *out)
{
    const char *scan = arrayStart;
    while (out->formatCount < MAX_STREAM_FORMATS) {
        const char *object = strstr(scan, "\"itag\":");
        if (!object || object >= regionEnd) break;
        const char *next = strstr(object + 7, "\"itag\":");
        const char *objectEnd = (next && next < regionEnd) ? next : regionEnd;

        StreamFormat *format = &out->formats[out->formatCount];
        memset(format, 0, sizeof *format);
        jsonInt(object, objectEnd, "itag", &format->itag);
        jsonInt(object, objectEnd, "width", &format->width);
        jsonInt(object, objectEnd, "height", &format->height);
        jsonInt(object, objectEnd, "fps", &format->fps);
        format->needsCipher = !jsonString(object, objectEnd, "url", format->url, sizeof format->url);

        char mimeType[64] = "";
        jsonString(object, objectEnd, "mimeType", mimeType, sizeof mimeType);
        strncpy(format->container, strstr(mimeType, "webm") ? "webm" : "mp4", sizeof format->container);
        int isAvc = strstr(mimeType, "avc1") != NULL || strstr(mimeType, "avc3") != NULL;
        int isAac = strstr(mimeType, "mp4a") != NULL;
        if (progressive) format->hasVideo = format->hasAudio = 1;
        else if (strncmp(mimeType, "audio/", 6) == 0) format->hasAudio = isAac;
        else format->hasVideo = isAvc;

        if (format->hasVideo || format->hasAudio) out->formatCount++;
        scan = objectEnd;
    }
}

static void parseCaptionTracks(const char *resp, const char *end, StreamInfo *out)
{
    const char *captions = strstr(resp, "\"captionTracks\"");
    if (!captions) return;
    const char *listEnd = strstr(captions, "\"audioTracks\"");
    if (!listEnd) listEnd = strstr(captions, "\"translationLanguages\"");
    if (!listEnd) listEnd = end;

    const char *scan = captions;
    while (out->captionCount < MAX_CAPTION_TRACKS) {
        const char *entry = strstr(scan, "\"baseUrl\"");
        if (!entry || entry >= listEnd) break;
        const char *next = strstr(entry + 1, "\"baseUrl\"");
        const char *entryEnd = (next && next < listEnd) ? next : listEnd;

        CaptionTrack *track = &out->captions[out->captionCount];
        if (jsonString(entry, entryEnd, "baseUrl", track->url, sizeof track->url) &&
            jsonString(entry, entryEnd, "languageCode", track->languageCode, sizeof track->languageCode)) {
            if (!jsonString(entry, entryEnd, "text", track->name, sizeof track->name) &&
                !jsonString(entry, entryEnd, "simpleText", track->name, sizeof track->name))
                strncpy(track->name, track->languageCode, sizeof track->name);
            out->captionCount++;
        }
        scan = entryEnd;
    }
    if (out->captionCount) logInfo("caption tracks: %d\n", out->captionCount);
}

static void parseFormats(const char *resp, const char *end, StreamInfo *out)
{
    const char *streaming = strstr(resp, "\"streamingData\"");
    if (!streaming) return;
    const char *progressive = strstr(streaming, "\"formats\"");
    const char *adaptive = strstr(streaming, "\"adaptiveFormats\"");
    if (progressive)
        parseFormatArray(progressive, (adaptive && adaptive > progressive) ? adaptive : end, 1, out);
    if (adaptive)
        parseFormatArray(adaptive, end, 0, out);
}

#define VISITOR_CACHE_PATH DATA_DIR "/visitor.txt"

static char visitorData[192];
static int visitorFromDisk;

static int loadVisitorData(void)
{
    return fileRead(VISITOR_CACHE_PATH, visitorData, sizeof visitorData) > 0 && visitorData[0] != 0;
}

static void saveVisitorData(void)
{
    fileWrite(VISITOR_CACHE_PATH, visitorData, (int)strlen(visitorData));
}

static void dropVisitorData(void)
{
    fileDelete(VISITOR_CACHE_PATH);
    visitorData[0] = 0;
    visitorFromDisk = 0;
}

static int ensureVisitorData(void)
{
    if (visitorData[0]) return 0;
    if (loadVisitorData()) { visitorFromDisk = 1; return 0; }

    HttpHeader headers[] = {
        { "Content-Type", "application/json" },
        { "User-Agent", WEB_UA },
        { "X-YouTube-Client-Name", "1" },
        { "X-YouTube-Client-Version", WEB_VERSION },
    };
    char *resp = malloc(RESP_CAP);
    if (!resp) return -1;

    int respLen = 0, status = 0;
    int rc = httpRequest("POST", VISITOR_URL, headers, 4, VISITOR_BODY, (int)strlen(VISITOR_BODY), resp, RESP_CAP, &respLen, &status);
    int ok = (rc == 0 && status == 200 && jsonString(resp, resp + respLen, "visitorData", visitorData, sizeof visitorData));
    free(resp);

    if (!ok) { logError("visitor_id failed rc=%d status=%d\n", rc, status); return -1; }
    visitorFromDisk = 0;
    logInfo("visitorData minted (%d chars)\n", (int)strlen(visitorData));
    return 0;
}

static void unescapeAmp(char *text)
{
    char *write = text;
    for (char *read = text; *read; ) {
        if (strncmp(read, "&amp;", 5) == 0) { *write++ = '&'; read += 5; }
        else *write++ = *read++;
    }
    *write = 0;
}

static void extractBetween(const char *text, const char *open, const char *close, char *out, int cap)
{
    out[0] = 0;
    const char *start = strstr(text, open);
    if (!start) return;
    start += strlen(open);
    const char *stop = strstr(start, close);
    if (!stop) return;
    int length = (int)(stop - start);
    if (length >= cap) length = cap - 1;
    memcpy(out, start, length);
    out[length] = 0;
}

static void parseLiveManifest(const char *resp, const char *end, StreamInfo *out)
{
    char manifestUrl[1400];
    if (!jsonString(resp, end, "dashManifestUrl", manifestUrl, sizeof manifestUrl)) return;

    int cap = 4 * 1024 * 1024;
    char *mpd = malloc(cap);
    if (!mpd) return;
    HttpHeader headers[] = { { "User-Agent", VR_UA }, { "Accept-Encoding", "identity" } };
    int length = 0, status = 0;
    if (httpRequest("GET", manifestUrl, headers, 2, NULL, 0, mpd, cap, &length, &status) != 0 || status != 200) {
        logError("live manifest fetch status=%d len=%d\n", status, length);
        free(mpd);
        return;
    }

    for (int i = 0; i < out->formatCount; i++) {
        StreamFormat *format = &out->formats[i];
        char idTag[24];
        snprintf(idTag, sizeof idTag, "id=\"%d\"", format->itag);
        const char *rep = strstr(mpd, idTag);
        if (!rep) continue;
        const char *repEnd = strstr(rep, "</Representation>");
        if (!repEnd) repEnd = mpd + length;

        char base[MAX_STREAM_URL];
        extractBetween(rep, "<BaseURL>", "</BaseURL>", base, sizeof base);
        if (!base[0] || strstr(rep, "<BaseURL>") > repEnd) continue;
        unescapeAmp(base);

        long startSq = -1, edgeSq = -1;
        for (const char *media = rep; (media = strstr(media, "media=\"sq/")) != NULL && media < repEnd; media++) {
            long number = 0;
            for (const char *digit = media + 10; *digit >= '0' && *digit <= '9'; digit++) number = number * 10 + (*digit - '0');
            if (startSq < 0) startSq = number;
            edgeSq = number;
        }
        if (edgeSq < 0) continue;

        format->isLiveSegmented = 1;
        format->liveStartSq = startSq;
        format->liveEdgeSq = edgeSq;
        snprintf(format->url, sizeof format->url, "dashseg|%ld|%ld|%s", startSq, edgeSq, base);
    }
    free(mpd);
}

static int extract(const char *input, StreamInfo *out)
{
    memset(out, 0, sizeof *out);

    char videoId[16];
    if (!extractVideoId(input, videoId, sizeof videoId)) return -1;
    if (ensureVisitorData() != 0) return -1;

    char body[1024];
    int bodyLen = snprintf(body, sizeof body, PLAYER_BODY_FMT, visitorData, videoId);

    HttpHeader headers[] = {
        { "Content-Type", "application/json" },
        { "User-Agent", VR_UA },
        { "X-YouTube-Client-Name", "28" },
        { "X-YouTube-Client-Version", VR_VERSION },
        { "X-Goog-Visitor-Id", visitorData },
        { "Origin", "https://www.youtube.com" },
        { "Accept-Encoding", "identity" },
    };

    char *resp = malloc(RESP_CAP);
    if (!resp) return -1;

    int respLen = 0, status = 0;
    int rc = httpRequest("POST", PLAYER_URL, headers, 7, body, bodyLen, resp, RESP_CAP, &respLen, &status);
    if (rc < 0) { logError("player POST failed rc=%d\n", rc); free(resp); return rc; }
    if (status != 200) { logError("player POST status=%d\n", status); free(resp); return -status; }
    const char *end = resp + respLen;

    const char *details = strstr(resp, "\"videoDetails\"");
    jsonString(details ? details : resp, end, "title", out->title, sizeof out->title);
    jsonString(details ? details : resp, end, "shortDescription", out->description, sizeof out->description);
    char lenText[16];
    if (jsonString(details ? details : resp, end, "lengthSeconds", lenText, sizeof lenText))
        out->durationSeconds = atoi(lenText);

    parseCaptionTracks(resp, end, out);
    parseFormats(resp, end, out);

    int liveNow = strstr(resp, "\"isLive\":true") != NULL;
    out->isLive = liveNow;
    if (liveNow)
        parseLiveManifest(resp, end, out);

    if (out->formatCount == 0) {
        char statusText[32] = "";
        const char *play = strstr(resp, "\"playabilityStatus\"");
        jsonString(play ? play : resp, end, "status", statusText, sizeof statusText);
        logError("no formats, respLen=%d playability=%s\n", respLen, statusText);
        if (visitorFromDisk && strcmp(statusText, "LOGIN_REQUIRED") == 0) dropVisitorData();
    } else {
        logInfo("resolved %d formats\n", out->formatCount);
        if (!visitorFromDisk) { saveVisitorData(); visitorFromDisk = 1; }
    }

    free(resp);
    return 0;
}

static void jsonEscape(const char *src, char *out, int cap)
{
    int i = 0;
    for (; *src && i < cap - 2; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') { out[i++] = '\\'; out[i++] = (char)c; }
        else if (c >= 0x20) out[i++] = (char)c;
    }
    out[i] = 0;
}

static void readField(const char *block, const char *end, const char *container, const char *key, char *out, int cap)
{
    const char *pos = strstr(block, container);
    if (pos && pos < end) jsonString(pos, end, key, out, cap);
}

static const char *nextItemBlock(const char *from)
{
    static const char *keys[] = { "\"videoRenderer\"", "\"gridVideoRenderer\"", "\"lockupViewModel\"" };
    const char *best = NULL;
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const char *hit = strstr(from, keys[i]);
        if (hit && (!best || hit < best)) best = hit;
    }
    return best;
}

static int isDuplicate(const SearchResults *out, const char *videoId)
{
    for (int i = 0; i < out->count; i++) if (strcmp(out->items[i].videoId, videoId) == 0) return 1;
    return 0;
}

static void readContentEndingWith(const char *from, const char *end, const char *suffix, char *out, int cap)
{
    int suffixLen = (int)strlen(suffix);
    const char *p = from;
    while ((p = strstr(p, "\"content\":\"")) != NULL && p + 11 <= end) {
        p += 11;
        const char *close = memchr(p, '"', end - p);
        if (!close) break;
        int length = (int)(close - p);
        if (length >= suffixLen && length < cap && strncmp(close - suffixLen, suffix, suffixLen) == 0) {
            memcpy(out, p, length); out[length] = 0; return;
        }
        p = close + 1;
    }
}

static void readChannelId(const char *block, const char *end, char *out, int cap)
{
    const char *p = strstr(block, "\"browseId\":\"UC");
    if (p && p < end) jsonString(p, end, "browseId", out, cap);
    else if (cap) out[0] = 0;
}

static int parseRendererItem(const char *block, const char *end, SearchResult *item)
{
    if (!jsonString(block, end, "videoId", item->videoId, sizeof item->videoId) || !item->videoId[0]) return 0;
    readChannelId(block, end, item->channelId, sizeof item->channelId);
    readField(block, end, "\"title\"", "text", item->title, sizeof item->title);
    if (!item->title[0])
        readField(block, end, "\"title\"", "simpleText", item->title, sizeof item->title);
    readField(block, end, "\"lengthText\"", "simpleText", item->duration, sizeof item->duration);
    readField(block, end, "\"ownerText\"", "text", item->author, sizeof item->author);
    if (!item->author[0])
        readField(block, end, "\"shortBylineText\"", "text", item->author, sizeof item->author);
    readField(block, end, "\"viewCountText\"", "simpleText", item->views, sizeof item->views);
    readField(block, end, "\"publishedTimeText\"", "simpleText", item->published, sizeof item->published);
    const char *livePos = strstr(block, "\"style\":\"LIVE\"");
    item->isLive = (livePos && livePos < end);
    return item->title[0] != 0;
}

static int parseLockupItem(const char *block, const char *end, SearchResult *item)
{
    const char *videoType = strstr(block, "\"contentType\":\"LOCKUP_CONTENT_TYPE_VIDEO\"");
    if (!videoType || videoType >= end) return 0;
    if (!jsonString(block, end, "contentId", item->videoId, sizeof item->videoId) || !item->videoId[0]) return 0;

    readChannelId(block, end, item->channelId, sizeof item->channelId);
    readField(block, end, "\"lockupMetadataViewModel\"", "content", item->title, sizeof item->title);
    readField(block, end, "\"metadataParts\"", "content", item->author, sizeof item->author);
    if (strstr(item->author, " views") || strstr(item->author, " watching")) item->author[0] = 0;
    readField(block, end, "\"thumbnailBadgeViewModel\"", "text", item->duration, sizeof item->duration);
    readContentEndingWith(block, end, " views", item->views, sizeof item->views);
    readContentEndingWith(block, end, " ago", item->published, sizeof item->published);
    item->isLive = 0;
    return item->title[0] != 0;
}

static void parseVideoResults(const char *resp, int respLen, SearchResults *out)
{
    const char *end = resp + respLen;
    const char *scan = resp;
    while (out->count < MAX_SEARCH_RESULTS) {
        const char *block = nextItemBlock(scan);
        if (!block) break;
        const char *next = nextItemBlock(block + 1);
        const char *blockEnd = (next && next < end) ? next : end;

        SearchResult *item = &out->items[out->count];
        memset(item, 0, sizeof *item);
        int ok = (strncmp(block + 1, "lockupViewModel", 15) == 0)
               ? parseLockupItem(block, blockEnd, item)
               : parseRendererItem(block, blockEnd, item);
        const char *membersBadge = strstr(block, "Members only");
        if (membersBadge && membersBadge < blockEnd) ok = 0;
        if (ok && !isDuplicate(out, item->videoId)) out->count++;
        scan = blockEnd;
    }

    const char *more = strstr(resp, "\"continuationItemRenderer\":{");
    if (more && more < end) jsonString(more, end, "token", out->continuation, sizeof out->continuation);

    const char *channelMeta = strstr(resp, "\"channelMetadataRenderer\":{");
    if (channelMeta && channelMeta < end) jsonString(channelMeta, end, "title", out->channelName, sizeof out->channelName);

    static const char *SORT_CHIP_LABELS[3] = { "Latest", "Popular", "Oldest" };
    for (int chip = 0; chip < 3; chip++) {
        char pat[48];
        snprintf(pat, sizeof pat, "\"chipViewModel\":{\"text\":\"%s\"", SORT_CHIP_LABELS[chip]);
        const char *found = strstr(resp, pat);
        if (found && found < end) jsonString(found, end, "token", out->sortTokens[chip], MAX_SORT_TOKEN);
    }
}

static int postAndParse(const char *url, const char *body, int bodyLen, SearchResults *out)
{
    memset(out, 0, sizeof *out);
    char *resp = malloc(RESP_CAP);
    if (!resp) return -1;

    HttpHeader headers[] = {
        { "Content-Type", "application/json" },
        { "User-Agent", WEB_UA },
        { "X-YouTube-Client-Name", "1" },
        { "X-YouTube-Client-Version", WEB_VERSION },
        { "Origin", "https://www.youtube.com" },
        { "Accept-Encoding", "identity" },
    };
    int headerCount = sizeof headers / sizeof headers[0];

    int respLen = 0, status = 0;
    int rc = httpRequest("POST", url, headers, headerCount, body, bodyLen, resp, RESP_CAP, &respLen, &status);
    if (rc < 0) { free(resp); return rc; }
    if (status != 200) { free(resp); return -status; }

    parseVideoResults(resp, respLen, out);
    free(resp);
    return 0;
}

static int search(const char *query, SortOrder sort, const char *continuation, SearchResults *out)
{
    char body[MAX_CONTINUATION + 512];
    int bodyLen;
    if (continuation) bodyLen = snprintf(body, sizeof body, CONTINUATION_BODY_FMT, continuation);
    else {
        char escaped[256];
        jsonEscape(query, escaped, sizeof escaped);
        if (sort < 0 || sort >= SORT_COUNT) sort = SORT_RELEVANCE;
        bodyLen = snprintf(body, sizeof body, SEARCH_BODY_FMT, escaped, SEARCH_SORT_PARAMS[sort]);
    }
    return postAndParse(SEARCH_URL, body, bodyLen, out);
}

static const struct { const char *browseId, *params; } TRENDING_FEEDS[TREND_COUNT] = {
    { "UCOpNcN46UbXVtpKMrmU4Abg", "Egh0cmVuZGluZw%3D%3D" },
    { "UCEgdi0XIXXZ-qJOFPf4JSKw", "EglzcG9ydHN0YWK4AQCSAwDyBgQKAjIA" },
    { "FEpodcasts_destination",   "qgcCCAM%3D" },
};

static int fetchTrending(TrendingCategory category, const char *continuation, SearchResults *out)
{
    if (category < 0 || category >= TREND_COUNT) return -1;
    char body[MAX_CONTINUATION + 512];
    int bodyLen;
    if (continuation) bodyLen = snprintf(body, sizeof body, CONTINUATION_BODY_FMT, continuation);
    else bodyLen = snprintf(body, sizeof body, TRENDING_BODY_FMT, TRENDING_FEEDS[category].browseId, TRENDING_FEEDS[category].params);
    return postAndParse(BROWSE_URL, body, bodyLen, out);
}

#define CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"
static int getChannel(const char *channelId, const char *token, SearchResults *out)
{
    char body[MAX_CONTINUATION + 512];
    int bodyLen;
    if (token) bodyLen = snprintf(body, sizeof body, CONTINUATION_BODY_FMT, token);
    else bodyLen = snprintf(body, sizeof body, TRENDING_BODY_FMT, channelId, CHANNEL_VIDEOS_PARAMS);
    return postAndParse(BROWSE_URL, body, bodyLen, out);
}

const Extractor youtubeExtractor = { "youtube", matches, extract, search, fetchTrending, getChannel };
