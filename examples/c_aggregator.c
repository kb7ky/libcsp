/*
 * c-aggregator - will connect/subscribe to a collection of broker publishers
 * and then publish(server) to interested parties
 */

#include <unistd.h>
#include <stdlib.h>
#include <zmq.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#define MAX_SUBSCRIBERS 50

int debug = 0;
bool quietMode = false;
const char *sub_str = "tcp://0.0.0.0:6000";
const char *pub_str = "tcp://0.0.0.0:7000";
zmq_pollitem_t items[MAX_SUBSCRIBERS];

typedef struct
{
    char *uri; // tcp://xxxx:port
    void *socket;
} subscribers_t;
int subscribers_count = 0;
subscribers_t subscribers[MAX_SUBSCRIBERS];
subscribers_t *subs = subscribers;

int main(int argc, char **argv)
{

    int ret = 0;
    int opt = 0;
    int i = 0;
    int hb_interval = 2000;
    int hb_timeout = 25000;
    int hb_remote_ttl = 7000;
    int rxcnt = 0;
    int txcnt = 0;
    int errcnt = 0;

    while ((opt = getopt(argc, argv, "dhs:p:f:t:q")) != -1)
    {
        switch (opt)
        {
        case 'd':
            debug++;
            break;
        case 's':
            if (subscribers_count > MAX_SUBSCRIBERS)
            {
                printf("Too many subscribers - max is %d\n", MAX_SUBSCRIBERS);
                exit(2);
            }
            subscribers[subscribers_count].uri = strdup(optarg);
            subscribers_count++;
            break;
        case 'p':
            pub_str = optarg;
            break;
        case 'q':
            quietMode = true;
            break;
        default:
            printf(
                "Usage:\n"
                " -d \t\tEnable debug\n"
                " -s SUB_STR\tsubscriber port: tcp://localhost:7000 (can have mutiple -s)\n"
                " -p PUB_STR\tpublisher  port: tcp://localhost:6000\n"
                " -q quiet mode - no logging\n");
            exit(1);
            break;
        }
    }

    void *ctx = zmq_ctx_new();
    assert(ctx);

    // frontend is the Publisher that will send all the aggregate data
    void *frontend = zmq_socket(ctx, ZMQ_PUB);
    assert(frontend);
    ret = zmq_bind(frontend, pub_str);
    assert(ret == 0);
    printf("Aggregator Publisher task started\n");

    // walk the subscribers and create the zmq sockets
    for (i = 0; i < subscribers_count; i++)
    {
        subscribers[i].socket = zmq_socket(ctx, ZMQ_SUB);
        zmq_setsockopt(subscribers[i].socket, ZMQ_SUBSCRIBE, "", 0);
        assert(zmq_setsockopt(subscribers[i].socket, ZMQ_HEARTBEAT_IVL, &hb_interval, sizeof(int)) == 0);
        assert(zmq_setsockopt(subscribers[i].socket, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(int)) == 0);
        assert(zmq_setsockopt(subscribers[i].socket, ZMQ_HEARTBEAT_TTL, &hb_remote_ttl, sizeof(int)) == 0);
        zmq_connect(subscribers[i].socket, subscribers[i].uri);
        if (debug)
        {
            printf("connected to Broker %s as SUB\n", subscribers[i].uri);
        }
    }
    printf("Aggregator connected %d subscribers\n", subscribers_count);

    /* setup HEARTBEAT on the Publisher connections */
    assert(zmq_setsockopt(frontend, ZMQ_HEARTBEAT_IVL, &hb_interval, sizeof(int)) == 0);
    assert(zmq_setsockopt(frontend, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(int)) == 0);
    assert(zmq_setsockopt(frontend, ZMQ_HEARTBEAT_TTL, &hb_remote_ttl, sizeof(int)) == 0);

    /* Wait for execution to end (ctrl+c) */
    while (1)
    {
        zmq_msg_t msg;
        uint8_t *rx_data;

        // fill items on every pass - used for results too!
        for (i = 0; i < subscribers_count; i++)
        {
            items[i].socket = subscribers[i].socket;
            items[i].events = ZMQ_POLLIN;
        }

        ret = zmq_poll(items, subscribers_count, 60 * 1000); // 60 sec
        if (ret > 0)
        {
            for (i = 0; i < subscribers_count; i++)
            {
                items[i].socket = subscribers[i].socket;
                if (items[i].revents == ZMQ_POLLIN)
                {
                    // this subscriber got a message
                    zmq_msg_init_size(&msg, 1024);
                    if (zmq_msg_recv(&msg, subscribers[i].socket, 0) < 0)
                    {
                        zmq_msg_close(&msg);
                        printf("ZMQ: subscriber %s error %s\n", subscribers[i].uri, zmq_strerror(zmq_errno()));
                        continue;
                    }

                    // publish message to aggregate subs
                    int datalen = zmq_msg_size(&msg);
                    rx_data = zmq_msg_data(&msg);
                    rx_data[datalen] = '\0';

                    rxcnt++;
                    if(debug) {
                        if((rxcnt % 1000) == 0) {
                            printf("%d",i);
                            fflush(stdout);
                        }
                    }

                    ret = zmq_send(frontend, rx_data, datalen, 0);
                    if (ret < 0)
                    {
                        if(zmq_errno() == EAGAIN) {
                            zmq_msg_close(&msg);
                            continue;
                        }
                        errcnt++;
                        printf("ZMQ: publisher %s error (%d) %s\n", pub_str, zmq_errno(), zmq_strerror(zmq_errno()));
                        printf("rxcnt %d txcnt %d\n", rxcnt, txcnt);
                    }

                    txcnt++;
                    if(debug) {
                        if((txcnt % 1000) == 0) {
                            printf("S");
                            fflush(stdout);
                        }
                    }

                    if (debug > 1)
                    {
                        printf("AGG: from: %s Len %d\n", subscribers[i].uri, datalen);
                        if (debug > 2)
                        {
                            printf(">%s<\n", rx_data);
                        }
                    }

                    zmq_msg_close(&msg);
                }
            }
        }
    }
    return 0;
}
