#include "ui.h"
#include "app.h"
#include "player.h"
#include "extractor.h"
#include "stream_select.h"
#include "storage.h"
#include "log.h"
#include "sponsorblock.h"
#include "subtitles.h"
#include "chapters.h"
#include "http.h"

#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/types.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_W 960
#define SCREEN_H 544

static SceCtrlData g_pad, g_prevPad;
static int g_running = 1;
static int g_lastPos = 0;

static SceWChar16 g_queryW[256];
static char g_query[256];
static int g_editing = 0;

static SearchResults g_feed;
static int g_selected = 0;
static int g_fetching = 0;
static int g_feedReady = 0;

static int g_inPlayer = 0;
static StreamInfo g_current;
static SponsorSegments g_sb;
static SubtitleTrack g_subTrack;
static ChapterList g_chapters;
static int g_subPick = -1;
static int g_subFetched = -1;
static int g_subOn = 0;
static int g_scanFrom = 0;
static int g_showChapters = 0;
static int g_showDesc = 0;

static void startSearch(const char *q)
{
    g_fetching = 1;
    g_feedReady = 0;
    g_selected = 0;
    memset(&g_feed, 0, sizeof g_feed);
    int rc = searchVideos(q, SORT_RELEVANCE, NULL, &g_feed);
    g_fetching = 0;
    g_feedReady = (rc == 0 && g_feed.count > 0);
    logInfo("search '%s' -> %d results\n", q, g_feed.count);
}

static void loadHome(void)
{
    g_fetching = 1;
    g_feedReady = 0;
    memset(&g_feed, 0, sizeof g_feed);
    int rc = getTrending(TREND_GAMING, NULL, &g_feed);
    g_fetching = 0;
    g_feedReady = (rc == 0 && g_feed.count > 0);
    logInfo("home feed -> %d results\n", g_feed.count);
}

static void openIme(void)
{
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.maxTextLength = sizeof(g_queryW) - 1;
    param.inputTextBuffer = g_queryW;
    param.title = L"Search YouTube";
    if (sceImeDialogInit(&param) >= 0)
        g_editing = 1;
}

static void updateIme(void)
{
    SceImeDialogStatus status;
    sceImeDialogGetStatus(&status);
    if (status == SCE_IME_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result;
        memset(&result, 0, sizeof result);
        sceImeDialogGetResult(&result);
        sceImeDialogTerm();
        g_editing = 0;
        for (int i = 0; i < 255 && g_queryW[i]; i++)
            g_query[i] = (char)g_queryW[i];
        g_query[255] = 0;
        if (result.button == SCE_IME_DIALOG_BUTTON_ENTER && g_query[0]) {
            if (strstr(g_query, "youtu") || strlen(g_query) == 11)
                playFromInput(g_query);
            else
                startSearch(g_query);
        }
    }
}

int extractVideoInfo(const char *input, StreamInfo *out)
{
    const Extractor *ex = findExtractor(input);
    if (!ex)
        return -1;
    return ex->extract(input, out);
}

static void playFromInput(const char *input)
{
    if (extractVideoInfo(input, &g_current) != 0) {
        logError("extract failed\n");
        return;
    }
    parseChapters(g_current.description, &g_chapters);
    g_sb.count = 0;
    fetchSponsorSegments(g_current.title, &g_sb);
    g_subPick = g_subFetched = g_subOn = 0;
    g_subTrack.count = 0;
    g_scanFrom = 0;
    g_showChapters = g_showDesc = 0;

    const StreamFormat *video = pickBestVideo(&g_current);
    if (!video) {
        logError("no playable video\n");
        return;
    }
    const StreamFormat *audio = video->hasAudio ? NULL : pickBestAudio(&g_current);

    if (playerOpen(video, audio) == 0)
        g_inPlayer = 1;
}

static void drawHome(void)
{
    uiBeginFrame();

    uiText(40, 40, "VitaTube - Trending", COLOR_ACCENT);
    uiText(40, 70, "[X] Play  [O] Back  [Start] Search", COLOR_DIM);

    if (g_fetching) {
        uiText(40, 140, "Loading...", COLOR_FG);
    } else if (!g_feedReady) {
        uiText(40, 140, "No results", COLOR_FG);
    } else {
        int cols = 4;
        int cellW = 220, cellH = 130;
        for (int i = 0; i < g_feed.count; i++) {
            int col = i % cols;
            int row = i / cols;
            int x = 40 + col * cellW;
            int y = 120 + row * cellH;
            unsigned int c = (i == g_selected) ? COLOR_ACCENT : COLOR_FG;
            uiRect(x, y, cellW - 12, cellH - 14, 0xFF1C2630);
            SearchResult *r = &g_feed.items[i];
            char line[160];
            snprintf(line, sizeof line, "%s", r->title);
            uiText(x + 6, y + 8, line, c);
            if (r->duration[0])
                uiText(x + 6, y + cellH - 30, r->duration, COLOR_DIM);
            if (r->author[0]) {
                snprintf(line, sizeof line, "%s", r->author);
                uiText(x + 6, y + cellH - 50, line, COLOR_DIM);
            }
        }
    }

    uiEndFrame();
}

static void drawPlayer(void)
{
    PlayerStatus st;
    playerUpdate(&st);

    uiBeginFrame();

    if (st.state == PLAYER_LOADING) {
        uiText(40, 260, "Buffering...", COLOR_FG);
    } else if (st.state == PLAYER_FAILED) {
        uiText(40, 260, "Playback failed", COLOR_ACCENT);
        uiText(40, 290, st.message, COLOR_FG);
    } else if (st.state == PLAYER_PLAYING) {
        playerDraw();
        char bar[256];
        snprintf(bar, sizeof bar, "%s", g_current.title);
        uiText(40, 20, bar, COLOR_FG);

        int total = st.duration;
        int pos = st.position;
        int pct = total > 0 ? (pos * 100 / total) : 0;
        snprintf(bar, sizeof bar, "%d:%02d / %d:%02d  [%d%%] %s",
                 pos / 60, pos % 60, total / 60, total % 60, pct, st.paused ? "[PAUSED]" : "");
        uiText(40, 500, bar, COLOR_DIM);

        if (g_subOn && g_subTrack.count > 0) {
            const char *cue = getSubtitleCueText(&g_subTrack, (float)pos, &g_scanFrom);
            if (cue)
                uiText(360, 460, cue, COLOR_FG);
        }
    }

    uiEndFrame();
}

static void updateHome(void)
{
    int count = g_feedReady ? g_feed.count : 0;
    if (count == 0)
        return;

    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_DOWN)) g_selected = (g_selected + 4) % count;
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_UP))   g_selected = (g_selected - 4 + count) % count;
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_RIGHT)) g_selected = (g_selected + 1) % count;
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_LEFT))  g_selected = (g_selected - 1 + count) % count;

    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_START))
        openIme();

    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_CROSS)) {
        char id[16];
        snprintf(id, sizeof id, "%s", g_feed.items[g_selected].videoId);
        playFromInput(id);
    }
}

static void updatePlayer(void)
{
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_CIRCLE)) {
        playerClose();
        g_inPlayer = 0;
        return;
    }
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_CROSS))
        playerTogglePause();

    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_TRIANGLE)) {
        if (g_current.captionCount > 0) {
            g_subPick++;
            if (g_subPick >= g_current.captionCount)
                g_subPick = -1;
            g_subOn = (g_subPick >= 0);
            if (g_subOn && g_subFetched != g_subPick) {
                fetchSubtitles(g_current.captions[g_subPick].url, &g_subTrack);
                g_subFetched = g_subPick;
                g_scanFrom = 0;
            }
        }
    }
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_RIGHT))
        playerSeek((float)(g_lastPos + 5));
    if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_LEFT))
        playerSeek((float)(g_lastPos - 5));
}

void appRun(void)
{
    loadHome();

    while (g_running) {
        uiReadPad(&g_pad, &g_prevPad);

        if (g_editing) {
            updateIme();
            uiBeginFrame();
            uiText(40, 260, "Enter text...", COLOR_FG);
            uiEndFrame();
            continue;
        }

        if (g_inPlayer) {
            PlayerStatus st;
            playerUpdate(&st);
            g_lastPos = st.position;
            updatePlayer();
            drawPlayer();
        } else {
            updateHome();
            drawHome();
        }

        if (uiPressed(&g_pad, &g_prevPad, SCE_CTRL_PSBUTTON))
            g_running = 0;
    }
}
