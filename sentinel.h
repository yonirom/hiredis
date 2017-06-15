#include "hiredis.h"

#define MAX_HOSTNAME_LEN 255

typedef struct sentinelList {
    char hostname[MAX_HOSTNAME_LEN];
    int port;
    struct sentinelList *next;
} sentinelList;

/* Context for Redis Sentinels */
typedef struct redisSentinelContext {
    int err; /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */
    int flags;

    sentinelList *list;

    struct timeval *timeout;

    redisContext *c;

} redisSentinelContext;


void redisSentinelFree(redisSentinelContext *c);
redisSentinelContext *redisSentinelInit(char **hostname, int *port, int len);
