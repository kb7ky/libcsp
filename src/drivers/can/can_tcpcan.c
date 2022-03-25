

#include <csp/drivers/can_tcpcan.h>

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <csp/csp_debug.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <csp/interfaces/csp_if_can.h>
// #include <linux/can.h>

#include <csp/csp.h>

// CAN interface data, state, etc.
typedef struct {
	char name[CSP_IFLIST_NAME_MAX + 1];
	csp_iface_t iface;
	csp_can_interface_data_t ifdata;
	pthread_t rx_thread;
	uint16_t rx_port;
	uint16_t tx_port;
	int rx_socket;
	int tx_socket;
	char hostname[CSP_HOSTNAME_MAX + 1];
	bool promisc;
} can_tcp_context_t;

/* fwd Decls */
static int csp_can_tcpcan_tx_connect(can_tcp_context_t * ctx);
static int csp_can_tcpcan_rx_connect(can_tcp_context_t * ctx);

static void tcpcan_free(can_tcp_context_t * ctx) {

	if (ctx) {
		if (ctx->tx_socket >= 0) {
			close(ctx->tx_socket);
		}
		if (ctx->rx_socket >= 0) {
			close(ctx->rx_socket);
		}
		free(ctx);
	}
}

static void * tcpcan_rx_thread(void * arg) {
	can_tcp_context_t * ctx = arg;

	while (1) {
		/* Read CAN frame from tcp connection*/
		int nbytes;
		int flgs = 0;
		uint8_t pcan_buff[PCAN_BUFF_SZ];


		if(ctx->rx_socket == -1) {
			/* socket closed for some reason */
			int res = csp_can_tcpcan_rx_connect(ctx);
			if(res != CSP_ERR_NONE) {
				csp_print("%s[%s]: socket failed to reopen. err %d - sleep 5\n",  __FUNCTION__, ctx->name, res);
				sleep(5);
				continue;
			}
		}

		memset(&pcan_buff[0], 0,PCAN_BUFF_SZ);
		nbytes = recv(ctx->rx_socket , &pcan_buff[0], PCAN_BUFF_SZ, flgs);

		if (nbytes < 0) {
			csp_print("%s[%s]: read() pcan failed, errno %d: %s\n", __FUNCTION__, ctx->name, errno, strerror(errno));
			close(ctx->rx_socket);
			ctx->rx_socket = -1;
			continue;
		}
		if(nbytes == 0) {
			/* socket closed - mark for reopen */
			close(ctx->rx_socket);
			ctx->rx_socket = -1;
			continue;
		}

		/* add inspection of the PCAN header here - specificall looking for CANFD */
		// XXX

		/* Strip flags */
		uint32_t can_id = ntohl(*((uint32_t*)&pcan_buff[24])) & 0x1fffffff;
		uint8_t dlc = pcan_buff[21];
		uint8_t * data = &pcan_buff[28];

		if (csp_dbg_packet_print >= 4)	{
			csp_print("%s[%s]: got DLC %d bytes from %d nbytes\n",__FUNCTION__, ctx->name, dlc, nbytes);
		}
		
		/* Call RX callbacsp_can_rx_frameck */
		csp_can_rx(&ctx->iface, can_id, data, dlc, NULL);
	}

	/* We should never reach this point */
	pthread_exit(NULL);
}

static int csp_can_tx_frame(void * driver_data, uint32_t id, const uint8_t * data, uint8_t dlc) {
	if (dlc > 8) {
		return CSP_ERR_INVAL;
	}

	can_tcp_context_t * ctx = driver_data;

	if(ctx->tx_socket == -1) {
#if 0
		if (csp_dbg_packet_print >= 4)	{
			csp_print("%s[%s]: socket is closed\n",__FUNCTION__, ctx->name);
		}
#endif
		return CSP_ERR_TX;
	}

	/* PCAN packet */
	S_CAN2IP frame={
		.sz = htons(0x24),
		.type = htons(0x80),
		.tag = 0,
		.timestamp64_lo = htonl(0),
		.timestamp64_hi = htonl(0),
		.channel = 0,
		.dlc = dlc,
		.flag = htons(2),
		.id = htonl(0),
		.data.val64u = 0
	};
	frame.id = id;
	if((frame.id & 0x1FFFFFFF)>0x7FF) {	// if CAN ID > 0x7FF always set EFF bit
		frame.id |= 1<<31;
	}
	frame.id= htonl(frame.id);

	memcpy(&frame.data.val8u, data, dlc);
	frame.dlc = dlc;
	
	send(ctx->tx_socket, &frame, 0x24, 0);

	return CSP_ERR_NONE;
}

int csp_can_tcpcan_set_promisc(const bool promisc, can_tcp_context_t * ctx) {
	return CSP_ERR_NONE;
}

static int csp_can_tcpcan_rx_connect(can_tcp_context_t * ctx) {
	int32_t optval = 1;
	int32_t retVal = 0;

	/* Create socket */
	if ((ctx->rx_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		csp_print("%s[%s]: socket() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		ctx->rx_socket = -1;
		return CSP_ERR_INVAL;
	}
	retVal = setsockopt(ctx->rx_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_SOCKET, SO_REUSEADDR) %s\n", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	// Enable keepalive packets
	retVal = setsockopt(ctx->rx_socket, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_SOCKET, SO_KEEPALIVE) %s", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	retVal = setsockopt(ctx->rx_socket, SOL_IP, IP_RECVERR, &optval, sizeof(int));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_IP, IP_RECVERR) %s", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ctx->rx_port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(ctx->rx_socket, (const struct sockaddr*) &addr, sizeof(addr)) < 0) {	    /* an Adresse binden */
		csp_print("%s[%s]: unable to bind socket - port %d\n", __FUNCTION__, ctx->name,ctx->rx_port);
		return CSP_ERR_INVAL;
    }

	csp_print("%s[%s]: RX Connected to %s:%d\n", __FUNCTION__, ctx->name, ctx->hostname, ctx->rx_port);

	return CSP_ERR_NONE;
}

static int csp_can_tcpcan_tx_connect(can_tcp_context_t * ctx) {
	int retVal = 0;
	int optval = 0;

	/* Create socket */
	if ((ctx->tx_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		csp_print("%s[%s]: socket() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		ctx->tx_socket = -1;
		return CSP_ERR_INVAL;
	}
	retVal = setsockopt(ctx->tx_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_SOCKET, SO_REUSEADDR) %s\n", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	// Enable keepalive packets
	retVal = setsockopt(ctx->tx_socket, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_SOCKET, SO_KEEPALIVE) %s", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	retVal = setsockopt(ctx->tx_socket, SOL_IP, IP_RECVERR, &optval, sizeof(int));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_IP, IP_RECVERR) %s", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ctx->tx_port);
	addr.sin_addr.s_addr = inet_addr(ctx->hostname);

	/* connect to the remote PCAN server for tx */
	if(connect(ctx->tx_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		csp_print("%s[%s]: Connect to %s:%d failed\n", __FUNCTION__, ctx->name, ctx->hostname, ctx->tx_port);
		ctx->tx_socket = -1;
		return CSP_ERR_INVAL;
	}

	/* Set filter mode */
	if (csp_can_tcpcan_set_promisc(ctx->promisc, ctx) != CSP_ERR_NONE) {
		csp_print("%s[%s]: csp_can_tcpcan_set_promisc() failed, error: %s\n", __FUNCTION__, ctx->name, strerror(errno));
		return CSP_ERR_INVAL;
	}

	csp_print("%s[%s]: TX Connected to %s:%d\n", __FUNCTION__, ctx->name, ctx->hostname, ctx->tx_port);

	return CSP_ERR_NONE;
}

int csp_can_tcpcan_open_and_add_interface(const char * ifname, csp_can_tcpcan_conf_t * ifconf, bool promisc, csp_iface_t ** return_iface) {
	int res = CSP_ERR_NONE;
	
	if (ifname == NULL) {
		ifname = CSP_TCPCAN_DEFAULT_NAME;
	}

	csp_print("INIT %s: host: [%s], promisc: %d\n", ifname, ifconf->host, promisc);

	can_tcp_context_t * ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		return CSP_ERR_NOMEM;
	}
	ctx->tx_socket = -1;
	ctx->tx_port = ifconf->tx_port;
	strncpy(ctx->hostname, ifconf->host, sizeof(ctx->hostname) - 1);

	ctx->rx_socket = -1;
	ctx->rx_port = ifconf->rx_port;

	if(ctx->tx_port == 0) {
		ctx->tx_port = PCAN_TX_UDP_PORT;
	}

	if(ctx->rx_port == 0) {
		ctx->rx_port = PCAN_RX_UDP_PORT;
	}

	strncpy(ctx->name, ifname, sizeof(ctx->name) - 1);
	ctx->iface.name = ctx->name;
	ctx->iface.interface_data = &ctx->ifdata;
	ctx->iface.driver_data = ctx;
	ctx->ifdata.tx_func = csp_can_tx_frame;
#if 1  // Needed in open source version
	ctx->ifdata.pbufs = NULL;
#endif
	ctx->promisc = promisc;

	if((res = csp_can_tcpcan_tx_connect(ctx)) != CSP_ERR_NONE) {
		csp_print("%s[%s]: csp_can_add_interfacecsp_can_tcpcan_tx_connect() failed, error: %d\n", __FUNCTION__, ctx->name, res);
		tcpcan_free(ctx);
		return res;		
	}

	if((res = csp_can_tcpcan_rx_connect(ctx)) != CSP_ERR_NONE) {
		csp_print("%s[%s]: csp_can_add_interfacecsp_can_tcpcan_rx_connect() failed, error: %d\n", __FUNCTION__, ctx->name, res);
		tcpcan_free(ctx);
		return res;		
	}

	/* Add interface to CSP */
	res = csp_can_add_interface(&ctx->iface);
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
