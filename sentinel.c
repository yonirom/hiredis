#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis.h"
#include "sentinel.h"

void _push_sentinel(sentinelList **head, char *hostname, int port)
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

void _promote_sentinel(sentinelList **head, sentinelList *item)
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


void print_list(sentinelList *head)
{
    while (head)
    {
        printf("%s %d\n", head->hostname, head->port);
        head = head->next;
    }
    printf("--\n");
}

static redisSentinelContext *redisSentinelContextInit(void) {
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

redisSentinelContext *redisSentinelInit(char **hostnames, int *ports, int len)
{
    redisSentinelContext *c;
    struct timeval tv;
    int i;

    c = redisSentinelContextInit();
    if (c == NULL)
        return NULL;

    for (i=0; i < len; i++)
        _push_sentinel(&(c->list), hostnames[i], ports[i]);

    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    if (c->timeout == NULL)
        c->timeout = malloc(sizeof(struct timeval));
    memcpy(c->timeout, &tv, sizeof(struct timeval));

    return c;
}

redisContext *redisSentinelConnect(redisSentinelContext *c)
{
    redisReply *reply;
    sentinelList dummy;
    sentinelList *iter = &dummy;

    iter->next = c->list;

    while ((iter = iter->next))
    {
        printf("looping with %s %d\n", iter->hostname, iter->port);
        c->c = redisConnectWithTimeout(iter->hostname, iter->port, *c->timeout);

        /* in case of connection error to all sentinels, return failed redisContext
        * to user */
        if (c->c == NULL || c->c->err)
        {
            redisFree(c->c);
            c->c = NULL;
            continue;
        }

        reply = (redisReply *)redisCommand(c->c,"SENTINEL get-master-addr-by-name mymaster");
        redisFree(c->c);
        c->c = NULL;
        if (reply->type == REDIS_REPLY_NIL)
        {
            freeReplyObject(reply);
            continue;
        }

        if (reply->type == REDIS_REPLY_ARRAY) {
            if (reply->elements == 2) // got a valid response for a master from the sentinel
            {
                for (int j = 0; j < reply->elements; j++)   //DEBUG
                    printf("%u) %s\n", j, reply->element[j]->str);

                // try to connect to actual redis
                c->c = redisConnectWithTimeout(reply->element[0]->str, atoi(reply->element[1]->str), *c->timeout);
                freeReplyObject(reply);
                if (c->c == NULL || c->c->err)
                {
                    redisFree(c->c);
                    c->c = NULL;
                    continue;
                }
                reply = (redisReply *)redisCommand(c->c,"ROLE");

                if (reply->type == REDIS_REPLY_ARRAY && !strncmp("master", reply->element[0]->str, 7))
                {
                    freeReplyObject(reply);
                    // advance iter to head
                    break;
                }
                redisFree(c->c);
                c->c = NULL;
            }

        }
    }
    printf("done. context is: %p\n", c->c);
    return c->c;
}





int main(int argc, char *argv[])
{
    char *hostnames[] = {"zoom", "booom", "localhost", "johnny", "test"};
    int ports[] = {4,4,26373,2,3};
    (void) argc;
    (void) argv;
    sentinelList *list;
    redisSentinelContext *c;
    redisContext *rc;

    c = redisSentinelInit(hostnames, ports, 5);
    list = c->list;

    print_list(list);

    //_promote_sentinel(&list, list->next);

    print_list(list);

    rc = redisSentinelConnect(c);
    if (rc) printf("%d %s\n", rc->err, rc->errstr);
    return 0;
}
