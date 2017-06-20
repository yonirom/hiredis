#include "sentinel.h"

/* Sentinel linked list helpers */
static void _free_sentinel_list(redisSentinelList **head)
{
    redisSentinelList *iter, *next;
    iter = *head;

    while (iter)
    {
        next = iter->next;
        free(iter);
        iter = next;
    }
    *head = NULL;
}

static void _push_sentinel(redisSentinelList **head, const char *hostname, int port)
{
    redisSentinelList *item, *iter;

    item = (redisSentinelList *)malloc(sizeof(redisSentinelList));
    strncpy(item->hostname, hostname, MAX_HOSTNAME_LEN);
    item->port = port;
    item->next = NULL;

    if (*head == NULL)
    {
        *head = item;
        return;
    }

    iter = *head;

    while (iter->next)
        iter = iter->next;
    iter->next = item;
}

static void _promote_sentinel(redisSentinelList **head, redisSentinelList *item)
{
    redisSentinelList *iter;

    if (*head == item)
        return;

    iter = *head;

    while (iter->next != item)
        iter = iter->next;

    iter->next = iter->next->next;
    item->next = *head;
    *head = item;
}
/* End of sentinel linked list helpers */


static redisSentinelContext *_redisSentinelContextInit(void) {
    redisSentinelContext *sc;

    sc = (redisSentinelContext *)calloc(1,sizeof(redisSentinelContext));
    if (sc == NULL)
        return NULL;

    sc->err = 0;
    sc->errstr[0] = '\0';
    sc->timeout = NULL;
    sc->list = NULL;

    return sc;
}

redisSentinelContext *redisSentinelInit(const char *cluster, const char **hostnames, const int *ports, int len)
{
    redisSentinelContext *sc;
    struct timeval tv = { 0, 200000 };
    int i;

    sc = _redisSentinelContextInit();
    if (sc == NULL)
        return NULL;

    for (i=0; i < len; i++)
        _push_sentinel(&(sc->list), hostnames[i], ports[i]);

    if (sc->timeout == NULL)
        sc->timeout = (struct timeval *)malloc(sizeof(struct timeval));
    memcpy(sc->timeout, &tv, sizeof(struct timeval));

    strncpy(sc->cluster, cluster, sizeof(sc->cluster));

    return sc;
}

void redisSentinelFree(redisSentinelContext *sc)
{
    if (sc == NULL)
        return;
    if (sc->list)
        _free_sentinel_list(&sc->list);
    if (sc->timeout)
        free(sc->timeout);
    free(sc);
}

redisContext *redisSentinelConnect(redisSentinelContext *sc)
{
    redisReply *reply = NULL;
    redisSentinelList dummy;
    redisSentinelList *iter = &dummy;

    iter->next = sc->list;

    while ((iter = iter->next))
    {
        printf("trying %s %d\n", iter->hostname, iter->port);
        sc->c = redisConnectWithTimeout(iter->hostname, iter->port, *sc->timeout);
        if (sc->c == NULL || sc->c->err)
        {
            redisFree(sc->c);
            continue;
        }

        reply = (redisReply *)redisCommand(sc->c,"SENTINEL get-master-addr-by-name %s", sc->cluster);

        if (reply->type == REDIS_REPLY_NIL)
        {
            freeReplyObject(reply);
            continue;
        }

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2)
        {
            // try to connect to actual redis
            redisFree(sc->c);
            sc->c = redisConnectWithTimeout(reply->element[0]->str, atoi(reply->element[1]->str), *sc->timeout);
            if (sc->c == NULL || sc->c->err)
            {
                redisFree(sc->c);
                continue;
            }

            freeReplyObject(reply);
            reply = (redisReply *)redisCommand(sc->c,"ROLE");

            if (reply->type == REDIS_REPLY_ARRAY && !strncmp("master", reply->element[0]->str, 7))
            {
                //Success
                freeReplyObject(reply);
                _promote_sentinel(&sc->list, iter);
                printf("done. context is: %p\n", (void *)sc->c);
                return sc->c;
            }
        }
    }
    printf("failed. context is: %p\n", (void *)sc->c);
    sc->c = (redisContext *)calloc(1, sizeof(redisContext));
    sc->c->err = REDIS_ERR;
    strncpy(sc->c->errstr, "Failed to connect to sentinels", 128);
    return sc->c;
}

redisAsyncContext *redisSentinelAsyncConnect(redisSentinelContext *sc)
{
    redisContext *c;
    c = redisSentinelConnect(sc);

    if (c == NULL)
        return NULL;

    sc->ac = redisAsyncUpgradeContext(c);

    if (sc->ac == NULL) {
        redisFree(c);
        return NULL;
    }

    return sc->ac;
}


redisContext *redisSentinelReconnect(redisSentinelContext *sc)
{
    return  redisSentinelConnect(sc);
}

/* Not implemented in hiredis
int redisSentinelAsyncReconnect(redisSentinelContext *sc)
{
    redisAsyncContext *ac;
    ac = redisSentinelAsyncConnect(sc);
    return ac == NULL ? REDIS_ERR : REDIS_OK;
}
*/
