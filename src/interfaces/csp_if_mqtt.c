#include <csp/interfaces/csp_if_mqtt.h>

#if (CSP_HAVE_LIBMQTT)

#include <mosquitto.h>
#include <assert.h>
#include <malloc.h>
#include <unistd.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>
#include "../csp_semaphore.h"
#include <pthread.h>

#include <csp/csp_id.h>

/* MQTT driver & interface */
typedef struct {
	struct mosquitto *mosq;
	pthread_t rx_thread;
	char host[16 + 1];
	uint16_t port;
	char publisherTopic[128 + 1];
	char subscriberTopic[128 + 1];
	char name[256 + 1];
	char user[256 + 1];
	char password[256 + 1];
	char aes256IV[256 + 1];
	char aes256Key[256 + 1];
	int encryptRX;
	int encryptTX;
	int flipTopics;
	int state;
	int sentid;
	csp_iface_t iface;
} mqtt_driver_t;

/* Linux is fast, so we keep it simple by having a single lock */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* forward DECLs */
void on_connect(struct mosquitto *mosq, void *obj, int rc);
void on_publish(struct mosquitto *mosq, void *obj, int mid);
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

/**
 * Interface transmit function
 * @param packet Packet to transmit
 * @return 1 if packet was successfully transmitted, 0 on error
 */
int csp_mqtt_tx(csp_iface_t * iface, uint16_t via, csp_packet_t * packet) {

	mqtt_driver_t * drv = iface->driver_data;

	/* pack the header */
	csp_id_prepend(packet);

	/* Print header data */
	if (csp_dbg_packet_print >= 3)	{
		csp_print("MQTTTX Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			packet->id.src, packet->id.dst, packet->id.dport,
			packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
	}

	int result = mosquitto_publish(drv->mosq, &drv->sentid, drv->publisherTopic, packet->frame_length, packet->frame_begin, 0, false);

	if (result != MOSQ_ERR_SUCCESS) {
		csp_print("MQTT send error: %u %s\n", result, mosquitto_strerror(result));
	}

	csp_buffer_free(packet);

	return CSP_ERR_NONE;
}

void * csp_mqtt_task(void * param) {
	mqtt_driver_t * drv = param;
	int rc = 0;

	while (1) {
		rc = mosquitto_loop(drv->mosq, -1, 1);
		if(rc){
			csp_print("IFMQTT: loop failed - connection error! (%d)\n", rc);
			sleep(10);
			mosquitto_reconnect(drv->mosq);
		}
	}

	return NULL;
}

int csp_mqtt_init(  uint16_t addr,
                    const char * ifname,
					const char * host,
                    uint16_t port,
                    const char * subscriberTopic,
                    const char * publisherTopic,
                    const char * user,
                    const char * password,
                    int encryptRX,
                    int encryptTX,
                    int flipTopics,
                    const char * aes256IV,
                    const char * aes256Key,
                    csp_iface_t ** return_interface) {

	int ret;
	pthread_attr_t attributes;
	char clientid[24];
	int rc = 0;

	mqtt_driver_t * drv = calloc(1, sizeof(*drv));
	assert(drv != NULL);

	if (ifname == NULL) {
		ifname = CSP_MQTTHUB_IF_NAME;
	}

	strncpy(drv->name, ifname, sizeof(drv->name) - 1);
	strncpy(drv->host, host, sizeof(drv->host) - 1);
	strncpy(drv->user, user, sizeof(drv->user) - 1);
	strncpy(drv->password, password, sizeof(drv->password) - 1);
	strncpy(drv->aes256IV, aes256IV, sizeof(drv->aes256IV) - 1);
	strncpy(drv->aes256Key, aes256Key, sizeof(drv->aes256Key) - 1);
	drv->port = port;
	drv->encryptRX = encryptRX;
	drv->encryptTX = encryptTX;
	drv->flipTopics = flipTopics;
	drv->iface.name = drv->name;
	drv->iface.driver_data = drv;
	drv->iface.nexthop = csp_mqtt_tx;
	drv->iface.mtu = CSP_MQTT_MTU;  // there is actually no 'max' MTU on MQTT, but assuming the other end is based on the same code

	if(drv->flipTopics) {
		/* used to test to if_mqtt back to back - crosses the topics so PUB -> SUB */
		strncpy(drv->subscriberTopic, publisherTopic, sizeof(drv->subscriberTopic) - 1);
		strncpy(drv->publisherTopic, subscriberTopic, sizeof(drv->publisherTopic) - 1);
	} else {
		strncpy(drv->publisherTopic, publisherTopic, sizeof(drv->publisherTopic) - 1);
		strncpy(drv->subscriberTopic, subscriberTopic, sizeof(drv->subscriberTopic) - 1);
	}
	csp_print("IFMQTT INIT %s: broker %s:%u\n      pubTopic: %s - subTopic: %s\n\n", drv->iface.name, drv->host, drv->port, drv->publisherTopic, drv->subscriberTopic);

	mosquitto_lib_init();

	memset(clientid, 0, 24);
	snprintf(clientid, 23, "if_mqtt_%d", getpid());
	drv->mosq = mosquitto_new(clientid, true, drv);
	mosquitto_int_option(drv->mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);
	mosquitto_threaded_set(drv->mosq, true);
		
	if(drv->mosq) {
		mosquitto_connect_callback_set(drv->mosq, on_connect);
		mosquitto_message_callback_set(drv->mosq, on_message);
		mosquitto_connect_callback_set(drv->mosq, on_connect);
		mosquitto_publish_callback_set(drv->mosq, on_publish);

	    rc = mosquitto_connect(drv->mosq, drv->host, drv->port, 60);
		if(rc != MOSQ_ERR_SUCCESS) {
			csp_print("MQTT Connect Error %s: broker %s:%u err: %d\n", drv->iface.name, drv->host, drv->port, rc);
		}

		mosquitto_subscribe(drv->mosq, NULL, drv->subscriberTopic, 0);

	}

	/* Start RX thread */
	ret = pthread_attr_init(&attributes);
	assert(ret == 0);
	ret = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	assert(ret == 0);
	ret = pthread_create(&drv->rx_thread, &attributes, csp_mqtt_task, drv);
	assert(ret == 0);

	/* Source Address to bind to interface */
	drv->iface.addr = addr;

	/* Register interface */
	csp_iflist_add(&drv->iface);

	if (return_interface) {
		*return_interface = &drv->iface;
	}

	return CSP_ERR_NONE;
}


void on_connect(struct mosquitto *mosq, void *obj, int rc) {
	mqtt_driver_t * drv = obj;
	if (csp_dbg_packet_print >= 4)	{
		if(rc != MOSQ_ERR_SUCCESS) {
			csp_print("IFMQTT %s:on_connect - failed %d %s\n", drv->iface.name, rc, mosquitto_connack_string(rc));
		} else {
			csp_print("IFMQTT %s:on_connect - success\n", drv->iface.name);
		}
	}

}

void on_publish(struct mosquitto *mosq, void *obj, int mid) {
	mqtt_driver_t * drv = obj;
	if (csp_dbg_packet_print >= 4)	{
		csp_print("IFMQTT %s: on_publish\n", drv->iface.name);
	}

}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message) {
	bool match = 0;
	mqtt_driver_t * drv = obj;
	csp_packet_t * packet;

	if (csp_dbg_packet_print >= 4)	{
		csp_print("IFMQTT %s: on_message - len %d - '%s' for topic '%s'\n", drv->iface.name, message->payloadlen, (char*) message->payload, message->topic);
	}

	mosquitto_topic_matches_sub(drv->subscriberTopic, message->topic, &match);
	if (match) {
		const csp_conf_t * conf = csp_get_conf();
		const uint32_t HEADER_SIZE = (conf->version == 2) ? 6 : 4;

		unsigned int datalen = message->payloadlen;
		if (datalen < HEADER_SIZE) {
			csp_print("MQTT RX %s: Too short datalen: %u - expected min %u bytes\n", drv->iface.name, datalen, HEADER_SIZE);
			return;
		}

		if ((datalen - HEADER_SIZE) > CSP_BUFFER_SIZE) {
			csp_print("MQTT RX %s: Too long datalen: %u - expected min %u bytes\n", drv->iface.name, datalen - HEADER_SIZE, CSP_BUFFER_SIZE);
			return;
		}

		// Create new csp packet
		packet = csp_buffer_get(datalen - HEADER_SIZE);
		if (packet == NULL) {
			csp_print("RX %s: Failed to get csp_buffer(%u) errno(%d)\n", drv->iface.name, datalen, csp_dbg_errno);
			drv->iface.drop++;
			return;
		}

		// Copy the data from mqtt to csp
		const uint8_t * rx_data = (uint8_t *) message->payload;

		csp_id_setup_rx(packet);

		memcpy(packet->frame_begin, rx_data, datalen);
		packet->frame_length = datalen;

		/* Parse the frame and strip the ID field */
		if (csp_id_strip(packet) != 0) {
			drv->iface.rx_error++;
			csp_buffer_free(packet);
			return;
		}

		/* Print header data */
		if (csp_dbg_packet_print >= 3)	{
			csp_print("MQTTRX Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
				packet->id.src, packet->id.dst, packet->id.dport,
				packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
		}

		// Route packet
		csp_qfifo_write(packet, &drv->iface, NULL);
	}
}

int csp_mqtt_setEncryption(int onoff) {
	pthread_mutex_lock(&lock);

	pthread_mutex_unlock(&lock);

	return CSP_ERR_NONE;
}

#endif  // CSP_HAVE_LIBMQTT
