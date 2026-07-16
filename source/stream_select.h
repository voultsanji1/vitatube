#ifndef VITATUBE_STREAM_SELECT_H
#define VITATUBE_STREAM_SELECT_H

#include "extractor.h"

const StreamFormat *pickBestVideo(const StreamInfo *info);
const StreamFormat *pickBestAudio(const StreamInfo *info);

#endif
