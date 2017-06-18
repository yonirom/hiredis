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
    redisSentinelList *new;
    redisSentinelList *iter;

    new = (redisSentinelList *)malloc(sizeof(redisSentinelList));
    strncpy(new->hostname, hostname, MAX_HOSTNAME_LEN);
    new->port = port;
    new->next = NULL;

    if (*head == NULL)
    {
        *head = new;
        return;
    }

    iter = *head;

    while (iter->next)
        iter = iter->next;
    iter->next = new;
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


/* We want the error field to be accessible directly instead of requiring
 * an indirection to the redisContext struct. */
static void __redisSentinelCopyError(redisSentinelContext *sc) {
    if (!sc)
        return;

    redisContext *c = sc->c;
    sc->err = c->err;
    strncpy(sc->errstr, c->errstr, 128);
}

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
    struct timeval tv = { 0, 100000 };
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
    // XXX We do not free the redisContext. This is the responsibility of the
    // user
    if (sc == NULL)
        return;
    if (sc->list)
        _free_sentinel_list(&sc->list);
    if (sc->timeout)
        free(sc->timeout);
    free(sc);
}

redisContext *redisSentinelGetRedisContext(redisSentinelContext *sc)
{
    return sc->c;
}

int redisSentinelConnect(redisSentinelContext *sc)
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
            goto next;

        reply = (redisReply *)redisCommand(sc->c,"SENTINEL get-master-addr-by-name %s", sc->cluster);

        if (reply->type == REDIS_REPLY_NIL)
            goto next;

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2)
        {
            // try to connect to actual redis
            redisFree(sc->c);
            sc->c = redisConnectWithTimeout(reply->element[0]->str, atoi(reply->element[1]->str), *sc->timeout);
            if (sc->c == NULL || sc->c->err)
                goto next;

            freeReplyObject(reply);
            reply = (redisReply *)redisCommand(sc->c,"ROLE");

            if (reply->type == REDIS_REPLY_ARRAY && !strncmp("master", reply->element[0]->str, 7))
            {
                //Success
                freeReplyObject(reply);
                _promote_sentinel(&sc->list, iter);
                break;
            }

        next:
            freeReplyObject(reply);
            __redisSentinelCopyError(sc);
            redisFree(sc->c);
            sc->c = NULL;
        }
    }
    printf("done. context is: %p\n", (void *)sc->c);
    return sc->c ? REDIS_CONNECTED : REDIS_ERR;
}

int redisSentinelReconnect(redisSentinelContext *sc)
{
    return redisSentinelConnect(sc);
}



/*
int main(int argc, char *argv[])
{
    char *hostnames[] = {"zoom", "booom", "localhost", "johnny", "test"};
    int ports[] = {4,4,26379,2,3};
    (void) argc;
    (void) argv;
    redisSentinelContext *c;
    redisContext *rc;

    c = redisSentinelInit(hostnames, ports, 5);

    print_list(c->list);

    //_promote_sentinel(&list, list->next);


    rc = redisSentinelConnect(c);
    if (rc) printf("%d %s\n", rc->err, rc->errstr);
    print_list(c->list);
    return 0;
}
*/

