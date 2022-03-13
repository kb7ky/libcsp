

#include <csp/drivers/can_tcpcan.h>

#include <pthread.h>
#include <stdlib.h>
#include <csp/csp_debug.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <csp/interfaces/csp_if_can.h>
#include <linux/can.h>

#include <csp/csp.h>

// CAN interface data, state, etc.
typedef struct {
	char name[CSP_IFLIST_NAME_MAX + 1];
	csp_iface_t iface;
	csp_can_interface_data_t ifdata;
	pthread_t rx_thread;
	int socket;
	int canport;
	uint16_t ecan240_port;
} can_tcp_context_t;

static void tcpcan_free(can_tcp_context_t * ctx) {

	if (ctx) {
		if (ctx->socket >= 0) {
			close(ctx->socket);
		}
		free(ctx);
	}
}

static void * tcpcan_rx_thread(void * arg) {
	can_tcp_context_t * ctx = arg;

	while (1) {
		/* Read CAN frame from tcp connection*/
		struct can_frame frame;
		int nbytes;
		ECAN240HDR ecan240hdr;

		// XXX - read from socket, remove ECAN-240 header
		nbytes = read(ctx->socket, &ecan240hdr, sizeof(ECAN240HDR));
		if (nbytes < 0) {
			csp_print("%s[%s]: read() ecr240hdr failed, errno %d: %s\n", __FUNCTION__, ctx->name, errno, strerror(errno));
			continue;
		}

		nbytes = read(ctx->socket, &frame, sizeof(frame));
		if (nbytes < 0) {
			csp_print("%s[%s]: read() can pdu failed, errno %d: %s\n", __FUNCTION__, ctx->name, errno, strerror(errno));
			continue;
		}

		if (nbytes != sizeof(frame)) {
			csp_print("%s[%s]: Read incomplete CAN frame, size: %d, expected: %u bytes\n", __FUNCTION__, ctx->name, nbytes, (unsigned int)sizeof(frame));
			continue;
		}

		/* Drop frames with standard id (CSP uses extended) */
		if (!(frame.can_id & CAN_EFF_FLAG)) {
			continue;
		}

		/* Drop error and remote frames */
		if (frame.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG)) {
			csp_print("%s[%s]: discarding ERR/RTR/SFF frame\n", __FUNCTION__, ctx->name);
			continue;
		}

		/* Strip flags */
		frame.can_id &= CAN_EFF_MASK;

		/* Call RX callbacsp_can_rx_frameck */
		csp_can_rx(&ctx->iface, frame.can_id, frame.data, frame.can_dlc, NULL);
	}

	/* We should never reach this point */
	pthread_exit(NULL);
}

static int csp_can_tx_frame(void * driver_data, uint32_t id, const uint8_t * data, uint8_t dlc) {
	if (dlc > 8) {
		return CSP_ERR_INVAL;
	}

	struct can_frame frame = {.can_id = id | CAN_EFF_FLAG,
							  .can_dlc = dlc};
	memcpy(frame.data, data, dlc);

	can_tcp_context_t * ctx = driver_data;
	ECAN240HDR ecan240hdr;

	// Add in ECAN-240 Transparent mode header
	ecan240hdr.canport = ctx->canport;
	ecan240hdr.flags = 14; // XXX - unknown how this field is defined

	if(write(ctx->socket, &ecan240hdr, sizeof(ECAN240HDR)) != sizeof(ECAN240HDR)) {
		csp_print("%s[%s]: write() failed to write ECR240 header, errno %d: %s\n", __FUNCTION__, ctx->name, errno, strerror(errno));
		return CSP_ERR_TX;
	}
	if(write(ctx->socket, &frame, sizeof(frame)) != sizeof(frame)) {
		csp_print("%s[%s]: write() failed to write CAN pdu, errno %d: %s\n", __FUNCTION__, ctx->name, errno, strerror(errno));
		return CSP_ERR_TX;
	}

	return CSP_ERR_NONE;
}


int csp_can_tcpcan_set_promisc(const bool promisc, can_tcp_context_t * ctx) {
	return CSP_ERR_NONE;
}


int csp_can_tcpcan_open_and_add_interface(const char * ifname, csp_can_tcpcan_conf_t * ifconf, bool promisc, csp_iface_t ** return_iface) {
	if (ifname == NULL) {
		ifname = CSP_TCPCAN_DEFAULT_NAME;
	}

	csp_print("INIT %s: host: [%s], promisc: %d\n", ifname, ifconf->host, promisc);

	can_tcp_context_t * ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		return CSP_ERR_NOMEM;
	}
	ctx->socket = -1;
	ctx->canport = ifconf->canport;
	ctx->ecan240_port = ifconf->ecan240_port;

	if(ctx->ecan240_port == 0) {
		ctx->ecan240_port = ECAN240_TCP_PORT;
	}

	strncpy(ctx->name, ifname, sizeof(ctx->name) - 1);
	ctx->iface.name = ctx->name;
	ctx->iface.interface_data = &ctx->ifdata;
	ctx->iface.driver_data = ctx;
	ctx->ifdata.tx_func = csp_can_tx_frame;
	ctx->ifdata.pbufs = NULL;

	/* Create socket */
	if ((ctx->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		csp_print("%s[%s]: socket() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		tcpcan_free(ctx);
		return CSP_ERR_INVAL;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ECAN240_TCP_PORT);
	addr.sin_addr.s_addr = inet_addr(ifconf->host);

	/* connect to the remote ECAN-240 server */
	if(connect(ctx->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		csp_print("%s[%s]: Connect to %s:%d failed\n", __FUNCTION__, ctx->name, ifconf->host, ctx->ecan240_port);
		tcpcan_free(ctx);
		return CSP_ERR_INVAL;
	}

	/* Set filter mode */
	if (csp_can_tcpcan_set_promisc(promisc, ctx) != CSP_ERR_NONE) {
		csp_print("%s[%s]: csp_can_tcpcan_set_promisc() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	/* Add interface to CSP */
	int res = csp_can_add_interface(&ctx->iface);
	if (res != CSP_ERR_NONE) {
		csp_print("%s[%s]: csp_can_add_interface() failed, error: %d\n", __FUNCTION__, ctx->name, res);
		tcpcan_free(ctx);
		return res;
	}

	/* Create receive thread */
	if (pthread_create(&ctx->rx_thread, NULL, tcpcan_rx_thread, ctx) != 0) {
		csp_print("%s[%s]: pthread_create() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		// tcpcan_free(ctx); // we already added it to CSP (no way to remove it)
		return CSP_ERR_NOMEM;
	}

	if (return_iface) {
		*return_iface = &ctx->iface;
	}

	return CSP_ERR_NONE;
}

int csp_can_tcpcan_stop(csp_iface_t * iface) {
	can_tcp_context_t * ctx = iface->driver_data;

	int error = pthread_cancel(ctx->rx_thread);
	if (error != 0) {
		csp_print("%s[%s]: pthread_cancel() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_DRIVER;
	}
	error = pthread_join(ctx->rx_thread, NULL);
	if (error != 0) {
		csp_print("%s[%s]: pthread_join() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_DRIVER;
	}
	tcpcan_free(ctx);
	return CSP_ERR_NONE;
}
