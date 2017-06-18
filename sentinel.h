#ifndef __HIREDIS_SENTINELH
#define __HIREDIS_SENTINELH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis.h"

#define MAX_HOSTNAME_LEN 255

#ifdef __cplusplus
extern "C" {
#endif

typedef struct redisSentinelList {
    char hostname[MAX_HOSTNAME_LEN];
    int port;
    struct redisSentinelList *next;
} redisSentinelList;

/* Context for Redis Sentinels */
typedef struct redisSentinelContext {
    int err; /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */
    int flags;
    struct timeval *timeout;
    redisContext *c;

    redisSentinelList *list;
    char cluster[256];

} redisSentinelContext;


void redisSentinelFree(redisSentinelContext *sc);
redisSentinelContext *redisSentinelInit(const char *cluster, const char **hostname, const int *port, int len);
int redisSentinelConnect(redisSentinelContext *sc);
int redisSentinelReconnect(redisSentinelContext *sc);
redisContext *redisSentinelGetRedisContext(redisSentinelContext *sc);

#ifdef __cplusplus
}
#endif

#endif
