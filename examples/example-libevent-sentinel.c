#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <sentinel.h>
#include <async.h>
#include <adapters/libevent.h>

redisSentinelContext *sc;

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) return;
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
    redisAsyncDisconnect(c);
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    redisAsyncContext *ac;
    struct event_base *base = event_base_new();
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    const char *hostnames[] = {"192.168.1.1", "192.168.1.5", "localhost", "192.168.1.1", "192.168.1.1"};
    const int ports[] = {4,4,26379,2,3};
    struct event_base *base = event_base_new();
    redisAsyncContext *ac;


    sc = redisSentinelInit("mymaster", hostnames, ports, 5);
    redisAsyncContext *c = redisSentinelAsyncConnect(sc);
    if (c == NULL || c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    redisLibeventAttach(c,base);
    redisAsyncSetConnectCallback(c,connectCallback);
    redisAsyncSetDisconnectCallback(c,disconnectCallback);
    redisAsyncCommand(c, NULL, NULL, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
    redisAsyncCommand(c, getCallback, (char*)"end-1", "blpop sleep 10");
    printf("cont...\n");
    ac = redisSentinelAsyncConnect(sc);
    redisLibeventAttach(ac,base);
    redisAsyncSetConnectCallback(ac,connectCallback);
    redisAsyncSetDisconnectCallback(ac,disconnectCallback);
    redisAsyncCommand(ac, getCallback, (char*)"end-1", "blpop sleep 10");
    printf("cont...2\n");
    event_base_dispatch(base);
    return 0;
}
