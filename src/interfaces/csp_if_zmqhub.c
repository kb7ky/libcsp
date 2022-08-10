#include <csp/interfaces/csp_if_zmqhub.h>

#if (CSP_HAVE_LIBZMQ)

#include <zmq.h>
#include <assert.h>
#include <malloc.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>
#include "../csp_semaphore.h"
#include <pthread.h>

#include <csp/csp_id.h>

/* ZMQ driver & interface */
typedef struct {
	pthread_t rx_thread;
	void * context;
	void * publisher;
	void * subscriber;
	char name[CSP_IFLIST_NAME_MAX + 1];
	int8_t * sent_addrs;					// table used to capture the csp_addresses we have sent to the Broker(network)
	csp_bin_sem_t sent_addrs_lock;
	int topiclen;							// bytes of header used for topic
	csp_iface_t iface;
} zmq_driver_t;

/* Linux is fast, so we keep it simple by having a single lock */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* forward DECLs */
static void sa_init(zmq_driver_t *drv);
static bool sa_check_addr(zmq_driver_t *drv, csp_packet_t *packet);
static void sa_set_addr(zmq_driver_t *drv, int addr);
static void set_filters(zmq_driver_t *drv, uint16_t addr, uint16_t hostmask, int version);


/**
 * Interface transmit function
 * @param packet Packet to transmit
 * @return 1 if packet was successfully transmitted, 0 on error
 */
int csp_zmqhub_tx(csp_iface_t * iface, uint16_t via, csp_packet_t * packet) {
	const csp_conf_t * conf = csp_get_conf();

	zmq_driver_t * drv = iface->driver_data;

	/* record the source csp address as "our" address */
	sa_set_addr(drv, packet->id.src);

	/* based on Mode, set CSP_FCMD_TLM and CSP_FSRC */
	switch(conf->mode) {
		case CSP_MODE_NONE:
			break;
		case CSP_MODE_CMDTX:
			packet->id.flags |= (CSP_FCMD | (conf->pktsrc << 5));
			break;
		case CSP_MODE_TLMTX:
			packet->id.flags |= (CSP_FTLM | (conf->pktsrc << 5));
			break;
		default:
			csp_print("ZMQTX: Invalide Mode set for this node %d\n",conf->mode);
			break;
	}

	/* pack the header */
	csp_id_prepend(packet);

	/* Print header data */
	if (csp_dbg_packet_print >= 3)	{
		csp_print("ZMQTX Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			packet->id.src, packet->id.dst, packet->id.dport,
			packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
	}

	if(drv->topiclen > 0) {
		packet->frame_begin -= drv->topiclen;
		packet->frame_length += drv->topiclen;

		uint8_t dest8 = (via != CSP_NO_VIA_ADDRESS) ? via : packet->id.dst;
		uint16_t dest16 = htobe16(dest8);

		switch(drv->topiclen) {
			case 1:
				memcpy(packet->frame_begin, &dest8, sizeof(dest8));
				break;
			case 2:
				memcpy(packet->frame_begin, &dest16, sizeof(dest16));
				break;
		}
	}

	/** 
	 * While a ZMQ context is thread safe, sockets are NOT threadsafe, so by sharing drv->publisher, we 
	 * need to have a lock around any calls that uses that */
	pthread_mutex_lock(&lock);
	int result = zmq_send(drv->publisher, packet->frame_begin, packet->frame_length, 0);
	pthread_mutex_unlock(&lock);

	if (result < 0) {
		csp_print("ZMQ send error: %u %s\n", result, zmq_strerror(zmq_errno()));
	}

	csp_buffer_free(packet);

	return CSP_ERR_NONE;
}

void * csp_zmqhub_task(void * param) {

	zmq_driver_t * drv = param;
	csp_packet_t * packet;
	const csp_conf_t * conf = csp_get_conf();
	const uint32_t HEADER_SIZE = (conf->version == 2) ? 6 : 4;

	while (1) {
		int ret;
		zmq_msg_t msg;

		ret = zmq_msg_init_size(&msg, CSP_ZMQ_MTU + HEADER_SIZE);
		assert(ret == 0);

		// Receive data
		if (zmq_msg_recv(&msg, drv->subscriber, 0) < 0) {
			csp_print("ZMQ RX err %s: %s\n", drv->iface.name, zmq_strerror(zmq_errno()));
			continue;
		}

		unsigned int datalen = zmq_msg_size(&msg);
		if (datalen < HEADER_SIZE) {
			csp_print("ZMQ RX %s: Too short datalen: %u - expected min %u bytes\n", drv->iface.name, datalen, HEADER_SIZE);
			zmq_msg_close(&msg);
			continue;
		}

		if ((datalen - HEADER_SIZE) > CSP_BUFFER_SIZE) {
			csp_print("ZMQ RX %s: Too long datalen: %u - expected min %u bytes\n", drv->iface.name, datalen - HEADER_SIZE, CSP_BUFFER_SIZE);
			zmq_msg_close(&msg);
			continue;
		}

		// Create new csp packet
		packet = csp_buffer_get(datalen - HEADER_SIZE);
		if (packet == NULL) {
			csp_print("RX %s: Failed to get csp_buffer(%u) errno(%d)\n", drv->iface.name, datalen, csp_dbg_errno);
			zmq_msg_close(&msg);
			drv->iface.drop++;
			continue;
		}

		// Copy the data from zmq to csp
		const uint8_t * rx_data = zmq_msg_data(&msg);

		// skip over the prepended topiclen
		if(drv->topiclen > 0) {
			rx_data += drv->topiclen;
			datalen -= drv->topiclen;
		}

		csp_id_setup_rx(packet);

		memcpy(packet->frame_begin, rx_data, datalen);
		packet->frame_length = datalen;

		/* Parse the frame and strip the ID field */
		if (csp_id_strip(packet) != 0) {
			drv->iface.rx_error++;
			csp_buffer_free(packet);
			continue;
		}

		/* Print header data */
		if (csp_dbg_packet_print >= 3)	{
			csp_print("ZMQRX Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			   packet->id.src, packet->id.dst, packet->id.dport,
			   packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
		}

		/* based on Mode, process CSP_FCMD_TLM and CSP_FSRC to squelch reflections */
		switch(conf->mode) {
			case CSP_MODE_NONE:
				break;
			case CSP_MODE_CMDTX:
				if((packet->id.flags & CSP_FTLM) == 0) {      /* CSP_FCMD == 0 */
					if (csp_dbg_packet_print >= 4)	{
						csp_print("ZMQRX Dropped in CMDTX mode\nPacket: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
							packet->id.src, packet->id.dst, packet->id.dport,
							packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
					}
					csp_buffer_free(packet);
					continue;
				}
				break;
			case CSP_MODE_TLMTX:
				if(packet->id.flags & CSP_FTLM) {
					if (csp_dbg_packet_print >= 4)	{
						csp_print("ZMQRX Dropped in TLMTX mode\nPacket: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
							packet->id.src, packet->id.dst, packet->id.dport,
							packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
					}
					csp_buffer_free(packet);
					continue;
				}
				break;
			default:
				csp_print("ZMQTX: Invalide Mode set for this node %d\n",conf->mode);
				break;
		}

		if(packet->id.src == drv->iface.addr || sa_check_addr(drv, packet) == false) {
			if (csp_dbg_packet_print >= 4)	{
				csp_print("ZMQRXDupe Packet: Src %u, Dst %u, Dport %u, Sport %u, Pri %u, Flags 0x%02X, Size %" PRIu16 "\n",
			   		packet->id.src, packet->id.dst, packet->id.dport,
			   		packet->id.sport, packet->id.pri, packet->id.flags, packet->length);
			}
			csp_buffer_free(packet);
			continue;
		}

		// Remove CGID FLAGS
		packet->id.flags = CSP_FLOCAL_REMOVE(packet->id.flags);
		if (csp_dbg_packet_print >= 4)	{
			csp_print("ZMQRX - flags after = %u\n", packet->id.flags);
		}

		// Route packet
		csp_qfifo_write(packet, &drv->iface, NULL);

		zmq_msg_close(&msg);
	}

	return NULL;
}

int csp_zmqhub_make_endpoint(const char * host, uint16_t port, char * buf, size_t buf_size) {
	int res = snprintf(buf, buf_size, "tcp://%s:%u", host, port);
	if ((res < 0) || (res >= (int)buf_size)) {
		buf[0] = 0;
		return CSP_ERR_NOMEM;
	}
	return CSP_ERR_NONE;
}

int csp_zmqhub_init(uint16_t addr,
					const char * host,
					uint32_t flags,
					uint16_t portoffset,
					int topiclen,
					csp_iface_t ** return_interface) {

	char pub[100];
	csp_zmqhub_make_endpoint(host, CSP_ZMQPROXY_SUBSCRIBE_PORT + portoffset, pub, sizeof(pub));

	char sub[100];
	csp_zmqhub_make_endpoint(host, CSP_ZMQPROXY_PUBLISH_PORT + portoffset, sub, sizeof(sub));

	return csp_zmqhub_init_w_endpoints(addr, pub, sub, flags, topiclen, return_interface);
}

int csp_zmqhub_init_w_endpoints(uint16_t addr,
								const char * publisher_endpoint,
								const char * subscriber_endpoint,
								uint32_t flags,
								int topiclen,
								csp_iface_t ** return_interface) {

	uint16_t * rxfilter = NULL;
	unsigned int rxfilter_count = 0;

	return csp_zmqhub_init_w_name_endpoints_rxfilter(addr,
													 NULL,
													 rxfilter, rxfilter_count,
													 publisher_endpoint,
													 subscriber_endpoint,
													 flags,
													 topiclen,
													 return_interface);
}

int csp_zmqhub_init_w_name_endpoints_rxfilter(const uint16_t addr,
											  const char * ifname,
											  const uint16_t rxfilter[], unsigned int rxfilter_count,
											  const char * publish_endpoint,
											  const char * subscribe_endpoint,
											  uint32_t flags,
											  int topiclen,
											  csp_iface_t ** return_interface) {

	int ret;
	pthread_attr_t attributes;
	zmq_driver_t * drv = calloc(1, sizeof(*drv));
	assert(drv != NULL);
	const csp_conf_t * conf = csp_get_conf();

	/* send_addrs init - allocate array and setup sem for updating send_addrs array */
	sa_init(drv);

	if (ifname == NULL) {
		ifname = CSP_ZMQHUB_IF_NAME;
	}

	strncpy(drv->name, ifname, sizeof(drv->name) - 1);
	drv->iface.name = drv->name;
	drv->iface.driver_data = drv;
	drv->iface.nexthop = csp_zmqhub_tx;
	drv->iface.mtu = CSP_ZMQ_MTU;  // there is actually no 'max' MTU on ZMQ, but assuming the other end is based on the same code

	drv->topiclen = topiclen;
	if(conf->version > 1) {
		drv->topiclen = 0;
	}

	/* NOTE: topiclen only valid for v1 - so header is 4 bytes */
	if(drv->topiclen > (CSP_PACKET_PADDING_BYTES - 4)) {
		csp_print("ZMQ INIT %s FAILED!!! : topiclen (%d) is larger than header buffer", drv->iface.name, drv->topiclen);
		assert(1);
	}

	drv->context = zmq_ctx_new();
	assert(drv->context != NULL);

	csp_print("ZMQ INIT %s: pub(tx): [%s]\n\t sub(rx): [%s]\n\t rx filters: %u\n", drv->iface.name, publish_endpoint, subscribe_endpoint, rxfilter_count);

	/* Publisher (TX) */
	drv->publisher = zmq_socket(drv->context, ZMQ_PUB);
	assert(drv->publisher != NULL);

	/* Subscriber (RX) */
	drv->subscriber = zmq_socket(drv->context, ZMQ_SUB);
	assert(drv->subscriber != NULL);

	// subscribe to all packets - no filter
	ret = zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, NULL, 0);
	assert(ret == 0);

	/* setup HEARTBEAT on the connections */
	int hb_interval = 2000;
	int hb_timeout = 5000;
	int hb_remote_ttl = 7000;
	assert(zmq_setsockopt(drv->publisher, ZMQ_HEARTBEAT_IVL, &hb_interval, sizeof(int)) == 0);
	assert(zmq_setsockopt(drv->publisher, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(int)) == 0);
	assert(zmq_setsockopt(drv->publisher, ZMQ_HEARTBEAT_TTL, &hb_remote_ttl, sizeof(int)) == 0);

	/* Connect to server */
	ret = zmq_connect(drv->publisher, publish_endpoint);
	assert(ret == 0);
	zmq_connect(drv->subscriber, subscribe_endpoint);
	assert(ret == 0);

	/* Start RX thread */
	ret = pthread_attr_init(&attributes);
	assert(ret == 0);
	ret = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	assert(ret == 0);
	ret = pthread_create(&drv->rx_thread, &attributes, csp_zmqhub_task, drv);
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

int csp_zmqhub_init_filter2(const char * ifname, const char * host, uint16_t addr, uint16_t netmask, int promisc, uint16_t portoffset, int topiclen, csp_iface_t ** return_interface) {
	char pub[100];
	csp_zmqhub_make_endpoint(host, CSP_ZMQPROXY_SUBSCRIBE_PORT + portoffset, pub, sizeof(pub));

	char sub[100];
	csp_zmqhub_make_endpoint(host, CSP_ZMQPROXY_PUBLISH_PORT + portoffset, sub, sizeof(sub));

	int ret;
	pthread_attr_t attributes;
	const csp_conf_t * conf = csp_get_conf();
	zmq_driver_t * drv = calloc(1, sizeof(*drv));
	assert(drv != NULL);

	/* send_addrs init - allocate array and setup sem for updating send_addrs array */
	sa_init(drv);

	if (ifname == NULL) {
		ifname = CSP_ZMQHUB_IF_NAME;
	}

	strncpy(drv->name, ifname, sizeof(drv->name) - 1);
	drv->iface.name = drv->name;
	drv->iface.driver_data = drv;
	drv->iface.nexthop = csp_zmqhub_tx;
	drv->iface.mtu = CSP_ZMQ_MTU;  // there is actually no 'max' MTU on ZMQ, but assuming the other end is based on the same code

	/* offset in zmq message used for topic to control resonance from broker */
	/* note: version 2 uses the csp header (pri | addr) so no topic len used */
	drv->topiclen = topiclen;
	if(conf->version > 1) {
		drv->topiclen = 0;
	}

	/* NOTE: topiclen only valid for v1 - so header is 4 bytes */
	if(drv->topiclen > (CSP_PACKET_PADDING_BYTES - 4)) {
		csp_print("ZMQ INIT %s FAILED!!! : topiclen (%d) is larger than header buffer", drv->iface.name, drv->topiclen);
		assert(1);
	}

	drv->context = zmq_ctx_new();
	assert(drv->context != NULL);

	csp_print("ZMQ INIT %s: pub(tx): [%s]\n\t sub(rx): [%s]\n", drv->iface.name, pub, sub);

	/* Publisher (TX) */
	drv->publisher = zmq_socket(drv->context, ZMQ_PUB);
	assert(drv->publisher != NULL);

	/* Subscriber (RX) */
	drv->subscriber = zmq_socket(drv->context, ZMQ_SUB);
	assert(drv->subscriber != NULL);

	/* Generate filters */
	uint16_t hostmask = (1 << (csp_id_get_host_bits() - netmask)) - 1;
	
	/* Connect to server */
	ret = zmq_connect(drv->publisher, pub);
	assert(ret == 0);
	zmq_connect(drv->subscriber, sub);
	assert(ret == 0);

	if (promisc) {

		// subscribe to all packets - no filter
		ret = zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, NULL, 0);
		assert(ret == 0);

	} else {
		set_filters(drv, addr, hostmask, conf->version);
	} 


	/* Start RX thread */
	ret = pthread_attr_init(&attributes);
	assert(ret == 0);
	ret = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	assert(ret == 0);
	ret = pthread_create(&drv->rx_thread, &attributes, csp_zmqhub_task, drv);
	assert(ret == 0);

	/* Register interface */
	csp_iflist_add(&drv->iface);

	if (return_interface) {
		*return_interface = &drv->iface;
	}

	return CSP_ERR_NONE;
}

static void set_filters(zmq_driver_t *drv, uint16_t addr, uint16_t hostmask, int version) {
	/* This needs to be static, because ZMQ does not copy the filter value to the
	* outgoing packet for each setsockopt call */
	static uint16_t filt[4][3];
	static uint8_t filt8[3];

	if(version != 1) {

		for (int i = 0; i < 4; i++) {
			//int i = CSP_PRIO_NORM;
			filt[i][0] = __builtin_bswap16((i << 14) | addr);
			filt[i][1] = __builtin_bswap16((i << 14) | addr | hostmask);
			filt[i][2] = __builtin_bswap16((i << 14) | 16383);
			zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt[i][0], 2);
			zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt[i][1], 2);
			zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt[i][2], 2);
		}
	} else {
		if(drv->topiclen > 0) {
			switch(drv->topiclen) {
				case 1:
					// topic is address, net address is on, and broadcast
					filt8[0] = (uint8_t)(addr & 0xFF);
					filt8[1] = (uint8_t)((addr | hostmask) & 0xFF);
					filt8[2] = 255;
					zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt8[0], 1);
					zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt8[1], 1);
					zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt8[2], 1);
					break;
				case 2:
					// topic is address, net address is on, and broadcast
					filt[0][0] = __builtin_bswap16(addr);
					filt[0][1] = __builtin_bswap16(addr | hostmask);
					filt[0][2] = __builtin_bswap16(16383);
					zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt[0][0], 2);
					zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt[0][1], 2);
					zmq_setsockopt(drv->subscriber, ZMQ_SUBSCRIBE, &filt[0][2], 2);
					break;
			}
		}
	}
	return;
}
/* sa_check_addr
 * check that we may have sent this packet to the broker.  if so, the broker will reflect it back
 * to us.  We use this to detect this condition
 * returns:
 *			true - this address is OK to receive (not a reflection)
 *    		false - this address is a reflection. Do not process
 */
static bool sa_check_addr(zmq_driver_t *drv, csp_packet_t *packet) {
	bool ret = true;

	// fast path - if we are the dst then take it reguardless of src
	if(packet->id.dst == drv->iface.addr) {
		return true;
	}

	// check if we sent this packet, and it is reflected back to us by the Broker
	// we use a counter so that should an address move to another interface, we will eventually 
	// figure this out
	csp_bin_sem_wait(&drv->sent_addrs_lock, 0);

	if(drv->sent_addrs[packet->id.src] > 0) {
		// we have sent packets with this as the source
		drv->sent_addrs[packet->id.src]--;
		ret = false;
	}

	csp_bin_sem_post(&drv->sent_addrs_lock);
	return ret;
}

/* sa_set_addr
 * set this address as we are sending it to the broker. later we will probably see it as a reflection
 */
static void sa_set_addr(zmq_driver_t *drv, int addr) {
	csp_bin_sem_wait(&drv->sent_addrs_lock,  0);

	// XXX - add range check
	drv->sent_addrs[addr]++;

	csp_bin_sem_post(&drv->sent_addrs_lock);
}

/*
 * send_addrs initialization
 * Note:
 *		v1 addresses are 5 bits long, v2 addresses are 14 bits long
 */
static void sa_init(zmq_driver_t *drv) {
	uint32_t addrbits = csp_id_get_host_bits();
	int addrsize = 2 ^ addrbits;
	drv->sent_addrs = calloc(1, addrsize);

	csp_bin_sem_init(&drv->sent_addrs_lock);
}


#endif  // CSP_HAVE_LIBZMQ
