#ifndef VITATUBE_PLAYER_H
#define VITATUBE_PLAYER_H

#include "extractor.h"

typedef enum {
    PLAYER_IDLE,
    PLAYER_LOADING,
    PLAYER_PLAYING,
    PLAYER_FAILED
} PlayerState;

typedef struct {
    PlayerState state;
    char message[128];
    int width;
    int height;
    int duration;
    int position;
    int paused;
} PlayerStatus;

int playerInit(void);
void playerTerm(void);

int playerOpen(const StreamFormat *video, const StreamFormat *audio);
void playerClose(void);

void playerUpdate(PlayerStatus *status);
void playerDraw(void);

void playerTogglePause(void);
void playerSeek(float seconds);
void playerSetVolume(float fraction);

#endif
