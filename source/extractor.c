#include "extractor.h"
#include "youtube.h"
#include <stddef.h>

extern const Extractor youtubeExtractor;

static const Extractor *registry[] = {
    &youtubeExtractor,
};

const Extractor *findExtractor(const char *input)
{
    for (unsigned i = 0; i < sizeof registry / sizeof registry[0]; i++)
        if (registry[i]->matches(input))
            return registry[i];
    return NULL;
}

int searchVideos(const char *query, SortOrder sort, const char *continuation, SearchResults *out)
{
    for (unsigned i = 0; i < sizeof registry / sizeof registry[0]; i++)
        if (registry[i]->search)
            return registry[i]->search(query, sort, continuation, out);
    return -1;
}

int getTrending(TrendingCategory category, const char *continuation, SearchResults *out)
{
    for (unsigned i = 0; i < sizeof registry / sizeof registry[0]; i++)
        if (registry[i]->getTrending)
            return registry[i]->getTrending(category, continuation, out);
    return -1;
}

int getChannelVideos(const char *channelId, const char *token, SearchResults *out)
{
    for (unsigned i = 0; i < sizeof registry / sizeof registry[0]; i++)
        if (registry[i]->getChannel)
            return registry[i]->getChannel(channelId, token, out);
    return -1;
}
