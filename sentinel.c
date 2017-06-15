#include "sentinel.h"

static void _free_sentinel_list(sentinelList **head)
{
    sentinelList *iter, *next;
    iter = *head;

    while (iter)
    {
        next = iter->next;
        free(iter);
        iter = next;
    }
    *head = NULL;
}

static void _push_sentinel(sentinelList **head, const char *hostname, int port)
{
    sentinelList *new;
    sentinelList *iter;

    new = (sentinelList *)malloc(sizeof(sentinelList));
    new->port = port;
    new->next = NULL;
    strncpy(new->hostname, hostname, MAX_HOSTNAME_LEN);

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

static void _promote_sentinel(sentinelList **head, sentinelList *item)
{
    sentinelList *iter;

    if (*head == item)
        return;

    iter = *head;

    while (iter->next != item)
        iter = iter->next;

    iter->next = iter->next->next;
    item->next = *head;
    *head = item;
}

/*
static void print_list(sentinelList *head)
{
    while (head)
    {
        printf("%s %d\n", head->hostname, head->port);
        head = head->next;
    }
    printf("--\n");
}
*/

static redisSentinelContext *_redisSentinelContextInit(void) {
    redisSentinelContext *c;

    c = (redisSentinelContext *)calloc(1,sizeof(redisSentinelContext));
    if (c == NULL)
        return NULL;

    c->err = 0;
    c->errstr[0] = '\0';
    c->timeout = NULL;
    c->list = NULL;

    return c;
}

redisSentinelContext *redisSentinelInit(const char *cluster, const char **hostnames, const int *ports, int len)
{
    redisSentinelContext *c;
    struct timeval tv = { 0, 100000 };
    int i;

    c = _redisSentinelContextInit();
    if (c == NULL)
        return NULL;

    for (i=0; i < len; i++)
        _push_sentinel(&(c->list), hostnames[i], ports[i]);

    if (c->timeout == NULL)
        c->timeout = malloc(sizeof(struct timeval));
    memcpy(c->timeout, &tv, sizeof(struct timeval));

    strncpy(c->cluster, cluster, sizeof(c->cluster));

    return c;
}

void redisSentinelFree(redisSentinelContext *c)
{
    // XXX We do not free the redisContext. This is the responsibility of the
    // user
    if (c == NULL)
        return;
    if (c->list)
        _free_sentinel_list(&c->list);
    if (c->timeout)
        free(c->timeout);
    free(c);
}

redisContext *redisSentinelConnect(redisSentinelContext *c)
{
    redisReply *reply = NULL;
    sentinelList dummy;
    sentinelList *iter = &dummy;

    iter->next = c->list;

    while ((iter = iter->next))
    {
        printf("looping with %s %d\n", iter->hostname, iter->port);
        c->c = redisConnectWithTimeout(iter->hostname, iter->port, *c->timeout);
        if (c->c == NULL || c->c->err)
            goto next;

        reply = (redisReply *)redisCommand(c->c,"SENTINEL get-master-addr-by-name %s", c->cluster);

        if (reply->type == REDIS_REPLY_NIL)
            goto next;

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2)
        {
            // try to connect to actual redis
            redisFree(c->c);
            c->c = redisConnectWithTimeout(reply->element[0]->str, atoi(reply->element[1]->str), *c->timeout);
            if (c->c == NULL || c->c->err)
                goto next;

            freeReplyObject(reply);
            reply = (redisReply *)redisCommand(c->c,"ROLE");

            if (reply->type == REDIS_REPLY_ARRAY && !strncmp("master", reply->element[0]->str, 7))
            {
                //Success
                freeReplyObject(reply);
                _promote_sentinel(&c->list, iter);
                break;
            }

        next:
            freeReplyObject(reply);
            redisFree(c->c);
            c->c = NULL;
        }
    }
    printf("done. context is: %p\n", (void *)c->c);
    return c->c;
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

