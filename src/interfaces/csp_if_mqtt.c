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

/* defines (local) */
#define MQTT_CRYPTO_BUF_MAX 4096	// max size of crypto buffer - allows for massive expansion

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
	int encryptRx;
	int encryptTx;
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
void on_disconnect(struct mosquitto *mosq, void *obj, int rc, const mosquitto_property *props);
int csp_mqtt_crypto_init(mqtt_driver_t *drv);
int csp_mqtt_crypto_rx(mqtt_driver_t *drv, void *inbuf, int inbuflen, void *outbug, int outbuflen, int *outlen);
int csp_mqtt_crypto_tx(mqtt_driver_t *drv, void *inbuf, int inbuflen, void *outbug, int outbuflen, int *outlen);

/**
 * Interface transmit function
 * @param packet Packet to transmit
 * @return 1 if packet was successfully transmitted, 0 on error
 */
int csp_mqtt_tx(csp_iface_t * iface, uint16_t via, csp_packet_t * packet) {
	int result = 0;
	mqtt_driver_t * drv = iface->driver_data;

	/* pack the header */
	csp_id_prepend(packet);

	/* Print header data */
	if (csp_dbg_packet_print >= 3)	{
		csp_print("MQTTTX Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			packet->id.src, packet->id.dst, packet->id.dport,
			packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
	}

	if(drv->encryptTx) {
		char outbuf[MQTT_CRYPTO_BUF_MAX];
		int outbuflen = 0;
		result = csp_mqtt_crypto_tx(drv, packet->frame_begin, packet->frame_length, outbuf, sizeof(outbuf), &outbuflen);
		result = mosquitto_publish(drv->mosq, &drv->sentid, drv->publisherTopic, outbuflen, outbuf, 0, false);
	} else {
		result = mosquitto_publish(drv->mosq, &drv->sentid, drv->publisherTopic, packet->frame_length, packet->frame_begin, 0, false);
	}

	if (result != MOSQ_ERR_SUCCESS) {
		csp_print("MQTT send error: %u %s\n", result, mosquitto_strerror(result));
	}

	csp_buffer_free(packet);

	return CSP_ERR_NONE;
}

void * csp_mqtt_task(void * param) {
	mqtt_driver_t * drv = param;

	while (1) {
		mosquitto_loop_forever(drv->mosq, -1, 1);
#if 0
		int rc = 0;
		rc = mosquitto_loop(drv->mosq, -1, 1);
		if(rc){
			csp_print("IFMQTT: loop failed - connection error! (%d)\n", rc);
			sleep(10);
			mosquitto_reconnect(drv->mosq);
		}
#endif
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
                    int encryptRx,
                    int encryptTx,
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
	drv->encryptRx = encryptRx;
	drv->encryptTx = encryptTx;
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

	/* init crypto stuff */
	csp_mqtt_crypto_init(drv);

	/* init mosquitto (mqtt) stuff */
	mosquitto_lib_init();

	// clientId MUST be unique in the broker or the connections will keep connecting/disconnecting
	memset(clientid, 0, 24);
	snprintf(clientid, 23, "if_mqtt_%d", drv->flipTopics);
	drv->mosq = mosquitto_new(clientid, true, drv);
	mosquitto_int_option(drv->mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);
	mosquitto_threaded_set(drv->mosq, true);

	if(drv->mosq) {
		mosquitto_connect_callback_set(drv->mosq, on_connect);
		mosquitto_message_callback_set(drv->mosq, on_message);
		mosquitto_publish_callback_set(drv->mosq, on_publish);
		mosquitto_disconnect_v5_callback_set(drv->mosq, on_disconnect);

		if(strlen(drv->user) != 0) {
			mosquitto_username_pw_set(drv->mosq, drv->user, drv->password);
		}

	    rc = mosquitto_connect(drv->mosq, drv->host, drv->port, 60);
		if(rc != MOSQ_ERR_SUCCESS) {
			csp_print("MQTT Connect Error %s: broker %s:%u err: %d\n", drv->iface.name, drv->host, drv->port, rc);
		}
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
	if(rc != MOSQ_ERR_SUCCESS) {
		csp_print("IFMQTT %s:on_connect - failed %d %s\n", drv->iface.name, rc, mosquitto_connack_string(rc));
	} else {
		csp_print("IFMQTT %s:on_connect - success - subscribing...\n", drv->iface.name);
		mosquitto_subscribe(drv->mosq, NULL, drv->subscriberTopic, 0);
	}
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
void on_publish(struct mosquitto *mosq, void *obj, int mid) {
	mqtt_driver_t * drv = obj;
	if (csp_dbg_packet_print >= 4)	{
		csp_print("IFMQTT %s: on_publish\n", drv->iface.name);
	}
}
#pragma GCC diagnostic pop

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

void on_disconnect(struct mosquitto *mosq, void *obj, int rc, const mosquitto_property *props) {
	mqtt_driver_t * drv = obj;
	if (csp_dbg_packet_print >= 4)	{
		csp_print("IFMQTT %s: on_disconnect - rc = %d\n", drv->iface.name, rc);
	}
}

/**
	Control Plane interface to turn encryption on/off
*/
int csp_mqtt_setEncryption(char *if_name, int txonoff, int rxonoff) {
	mqtt_driver_t *drv = NULL;

	csp_iface_t * ifc = csp_iflist_get();
	while (ifc) {
        if (strncmp(ifc->name, if_name, CSP_IFLIST_NAME_MAX) == 0) {
        	drv = ifc->driver_data;
        }
		ifc = ifc->next;
	}

	if(drv == NULL) {
		csp_print("IFMQTT: setEncryption failed to fine IFNAME %s\n",if_name);
		return CSP_ERR_INVAL;
	} else {
		csp_print("IFMQTT:%s setEncryption Tx %d Rx %d\n",if_name, txonoff, rxonoff);
	}

	// Lock since this could be on another thread (python)
	pthread_mutex_lock(&lock);
	drv->encryptTx = txonoff;
	drv->encryptRx = rxonoff;
	pthread_mutex_unlock(&lock);

	return CSP_ERR_NONE;
}

int csp_mqtt_getEncryption(char *if_name, int *txonoff, int *rxonoff) {
	mqtt_driver_t *drv = NULL;

	csp_iface_t * ifc = csp_iflist_get();
	while (ifc) {
        if (strncmp(ifc->name, if_name, CSP_IFLIST_NAME_MAX) == 0) {
        	drv = ifc->driver_data;
        }
		ifc = ifc->next;
	}

	if(drv == NULL) {
		csp_print("IFMQTT: getEncryption failed to fine IFNAME %s\n",if_name);
		return CSP_ERR_INVAL;
	} else {
		csp_print("IFMQTT:%s getEncryption Tx %d Rx %d\n",if_name, drv->encryptTx, drv->encryptRx);
	}

	*txonoff = drv->encryptTx;
	*rxonoff = drv->encryptRx;

	return CSP_ERR_NONE;}

int csp_mqtt_crypto_init(mqtt_driver_t *drv) {
	return CSP_ERR_NONE;
}

int csp_mqtt_crypto_rx(mqtt_driver_t *drv, void *inbuf, int inbuflen, void *outbug, int outbuflen, int *outlen) {
	*outlen = 0;
	return CSP_ERR_NONE;
}

int csp_mqtt_crypto_tx(mqtt_driver_t *drv, void *inbuf, int inbuflen, void *outbug, int outbuflen, int *outlen) {
	*outlen = 0;
	return CSP_ERR_NONE;
}

#endif  // CSP_HAVE_LIBMQTT
