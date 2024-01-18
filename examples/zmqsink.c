#include <unistd.h>
#include <stdlib.h>
#include <zmq.h>
#include <assert.h>
#include <pthread.h>

int debug = 0;
const char * pub_str = "tcp://0.0.0.0:7000";
char * logfile_name = NULL;

FILE * logfile;

static void * sink_capture(void * ctx) {

    int ret;

	printf("Sink task listening on %s\n", pub_str);

	/* Subscriber (RX) */
	void * subscriber = zmq_socket(ctx, ZMQ_SUB);
	ret = zmq_connect(subscriber, pub_str);
    assert(ret == 0);
	ret = zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
    assert(ret == 0);

	while (1) {
		zmq_msg_t msg;
		zmq_msg_init_size(&msg, 2048);

		/* Receive data */
		if (zmq_msg_recv(&msg, subscriber, 0) < 0) {
			zmq_msg_close(&msg);
			printf("ZMQ: %s\n", zmq_strerror(zmq_errno()));
			continue;
		}

		int datalen = zmq_msg_size(&msg);
		if (datalen < 5) {
			printf("ZMQ: Too short datalen: %u\n", datalen);
			while (zmq_msg_recv(&msg, subscriber, ZMQ_NOBLOCK) > 0)
				zmq_msg_close(&msg);
			continue;
		}

		if (datalen >=  2048) {
			printf("ZMQ: Too long datalen: %u\n", datalen);
			while (zmq_msg_recv(&msg, subscriber, ZMQ_NOBLOCK) > 0)
				zmq_msg_close(&msg);
			continue;
		}

        printf("R: %d\n", datalen);

		zmq_msg_close(&msg);
	}
}

int main(int argc, char ** argv) {

    int ret = 0;

	int opt;
	while ((opt = getopt(argc, argv, "dp:")) != -1) {
		switch (opt) {
			case 'd':
				debug = 1;
				break;
			case 'p':
				pub_str = optarg;
				break;
			default:
				printf(
					"Usage:\n"
					" -d \t\tEnable debug\n"
					" -p PUB_STR\tpublisher  port: tcp://localhost:7000\n");
				exit(1);
				break;
		}
	}

	void * ctx = zmq_ctx_new();
	assert(ctx);

	sink_capture(ctx);

	printf("Closing ZMQsinc");
	zmq_ctx_destroy(ctx);

	return ret;
}
