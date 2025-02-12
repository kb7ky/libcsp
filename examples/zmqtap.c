#include <unistd.h>
#include <stdlib.h>
#include <zmq.h>
#include <assert.h>
#include <pthread.h>

#include <csp/csp.h>

int csp_id_strip(csp_packet_t * packet);
int csp_id_setup_rx(csp_packet_t * packet);
extern csp_conf_t csp_conf;

int debug = 0;
const char * pub_str = "tcp://0.0.0.0:7000";
char * logfile_name = NULL;
int topiclen = 0;
uint32_t topic = 0;
uint16_t topic16 = 0;

FILE * logfile;

static void * tap_capture(void * ctx) {

    int ret;
	uint8_t *rx_data;

	csp_print("Capture/logging task listening on %s version %d\n", pub_str, csp_conf.version);

	/* Subscriber (RX) */
	void * subscriber = zmq_socket(ctx, ZMQ_SUB);
	ret = zmq_connect(subscriber, pub_str);
    assert(ret == 0);
	ret = zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
    assert(ret == 0);

	/* Allocated 'raw' CSP packet */
	csp_packet_t * packet = malloc(2048);
	assert(packet != NULL);

	if (logfile_name) {
		logfile = fopen(logfile_name, "a+");
		if (logfile == NULL) {
			csp_print("Unable to open logfile %s\n", logfile_name);
			exit(-1);
		}
	}

	while (1) {
		zmq_msg_t msg;
		zmq_msg_init_size(&msg, 2048);

		/* Receive data */
		if (zmq_msg_recv(&msg, subscriber, 0) < 0) {
			zmq_msg_close(&msg);
			csp_print("ZMQ: %s\n", zmq_strerror(zmq_errno()));
			continue;
		}

		int datalen = zmq_msg_size(&msg);
		if (datalen < 5) {
			csp_print("ZMQ: Too short datalen: %u\n", datalen);
			while (zmq_msg_recv(&msg, subscriber, ZMQ_NOBLOCK) > 0)
				zmq_msg_close(&msg);
			continue;
		}

		if (datalen >=  2048) {
			csp_print("ZMQ: Too long datalen: %u\n", datalen);
			while (zmq_msg_recv(&msg, subscriber, ZMQ_NOBLOCK) > 0)
				zmq_msg_close(&msg);
			continue;
		}

		rx_data = zmq_msg_data(&msg);

		if(topiclen > 0) {
			switch(topiclen) {
				case 1:
					topic = (int)(*((uint8_t *)(rx_data)));
					break;
				case 2:
					// topic = (int)(*((uint16_t *)(rx_data)));
					memcpy(&topic16, rx_data, sizeof(uint16_t));
					topic = (int)(topic16);
					break;
			}
			rx_data += topiclen;
			datalen -= topiclen;
		}

		/* Copy to packet */
		csp_id_setup_rx(packet);
		memcpy(packet->frame_begin, rx_data, datalen);
		packet->frame_length = datalen;

		/* Parse header */
		csp_id_strip(packet);

		/* Print header data */
		if(topiclen > 0) {
			csp_print("Topic: %04X, Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			   topic,
			   packet->id.src, packet->id.dst, packet->id.dport,
			   packet->id.sport, packet->id.pri, packet->id.flags, packet->length);

		} else {
			csp_print("Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			   packet->id.src, packet->id.dst, packet->id.dport,
			   packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
		}	   

		if (logfile) {
			const char * delimiter = "--------\n";
			fwrite(delimiter, sizeof(delimiter), 1, logfile);
			fwrite(packet->frame_begin, packet->frame_length, 1, logfile);
			fflush(logfile);
		}

		zmq_msg_close(&msg);
	}
}

int main(int argc, char ** argv) {

    int ret = 0;
	csp_conf.version = 1;

	int opt;
	while ((opt = getopt(argc, argv, "dhv:p:f:t:")) != -1) {
		switch (opt) {
			case 'd':
				debug = 1;
				break;
			case 'v':
				csp_conf.version = atoi(optarg);
				break;
			case 'p':
				pub_str = optarg;
				break;
			case 'f':
				logfile_name = optarg;
				break;
			case 't':
				topiclen = atoi(optarg);
				break;
			default:
				csp_print(
					"Usage:\n"
					" -d \t\tEnable debug\n"
					" -v VERSION\tcsp version\n"
					" -p PUB_STR\tpublisher  port: tcp://localhost:7000\n"
					" -t TOPICLEN\tTopicLength in front of csp packet (1 or 2). Only valid with version 1\n"
					" -f LOGFILE\tLog to this file\n");
				exit(1);
				break;
		}
	}

	void * ctx = zmq_ctx_new();
	assert(ctx);

	tap_capture(ctx);

	csp_print("Closing ZMQtap");
	zmq_ctx_destroy(ctx);

	return ret;
}
