#include <csp/drivers/tcp_kiss.h>

#include <csp/csp_debug.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>
#include <malloc.h>

#include <csp/csp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

typedef struct {
	csp_tcp_callback_t rx_callback;
	void * user_data;
	int socket;
	pthread_t rx_thread;
} tcp_context_t;

/* Linux is fast, so we keep it simple by having a single lock */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void csp_tcp_lock(void * driver_data) {
	pthread_mutex_lock(&lock);
}

void csp_tcp_unlock(void * driver_data) {
	pthread_mutex_unlock(&lock);
}

static void * tcp_rx_thread(void * arg) {

	tcp_context_t * ctx = arg;
	const unsigned int CBUF_SIZE = 400;
	uint8_t * cbuf = malloc(CBUF_SIZE);

	// Receive loop
	while (1) {
		int length = read(ctx->socket, cbuf, CBUF_SIZE);
		if (length <= 0) {
			csp_print("%s: read() failed, returned: %d\n", __FUNCTION__, length);
			exit(1);
		}
		ctx->rx_callback(ctx->user_data, cbuf, length, NULL);
	}
	return NULL;
}

int csp_tcp_write(csp_tcp_fd_t fd, const void * data, size_t data_length) {

	if (fd >= 0) {
		int res = write(fd, data, data_length);
		if (res >= 0) {
			return res;
		}
	}
	return CSP_ERR_TX;  // best matching CSP error code.
}

int csp_tcp_open(const csp_tcp_conf_t * conf, csp_tcp_callback_t rx_callback, void * user_data, csp_tcp_fd_t * return_fd) {
	int retVal = 0;
	int optval = 0;


	tcp_context_t * ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		csp_print("%s: Error allocating context, host: [%s], errno: %s\n", __FUNCTION__, conf->host, strerror(errno));
		return CSP_ERR_NOMEM;
	}
	ctx->rx_callback = rx_callback;
	ctx->user_data = user_data;

	/* Create socket */
	if ((ctx->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		csp_print("%s[%s]: socket() failed, error: %s\n", __FUNCTION__, conf->host, strerror(errno));
		ctx->socket = -1;
		free(ctx);
		return CSP_ERR_INVAL;
	}
	// Enable keepalive packets
	retVal = setsockopt(ctx->socket, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
	if(retVal < 0){
		csp_print("%s[%s]: error using setsockopt (SOL_SOCKET, SO_KEEPALIVE) %s", __FUNCTION__, conf->host, strerror(errno));
		close(ctx->socket);
		free(ctx);
		return CSP_ERR_INVAL;
	}

	struct sockaddr_in addr;
	struct sockaddr_in clientaddr;
	int newS = -1;
	socklen_t clientaddrlen = 0;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(conf->port);

	if(conf->listen) {
		// XXX - testing ONLY! will accept one connection!
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if((bind(ctx->socket, (struct sockaddr *)(&addr), sizeof(addr))) != 0) {
			csp_print("%s[%s]: failed to bind\n", __FUNCTION__, conf->host, strerror(errno));
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_INVAL;
		}
		if((listen(ctx->socket, 1)) != 0) {
			csp_print("%s[%s]: failed to listen\n", __FUNCTION__, conf->host, strerror(errno));
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_INVAL;
		}
		if(((newS = accept(ctx->socket, (struct sockaddr *)(&clientaddr), &clientaddrlen))) < 0) {
			csp_print("%s[%s]: failed to accept\n", __FUNCTION__, conf->host, strerror(errno));
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_INVAL;
		}
		close(ctx->socket);
		ctx->socket = newS;
	} else {
		struct in_addr remote;
		if (inet_aton(conf->host, &remote) == 0) {
			csp_print("  Bad remote address %s\n", conf->host);
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_INVAL;
		}
		addr.sin_addr.s_addr = htonl(remote.s_addr);
		if(connect(ctx->socket, (struct sockaddr *)(&addr), sizeof(addr)) < 0) {
			csp_print("%s[%s]: failed to connect\n", __FUNCTION__, conf->host, strerror(errno));
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_INVAL;
		}
		if (csp_dbg_packet_print >= 1)	{
			csp_print("%s[%s]: connected to %s\n",__FUNCTION__, conf->host, conf->host);
		}
	}

	if (rx_callback) {
		int ret;
		pthread_attr_t attributes;

		ret = pthread_attr_init(&attributes);
		if (ret != 0) {
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_NOMEM;
		}
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&ctx->rx_thread, &attributes, tcp_rx_thread, ctx);
		if (ret != 0) {
			csp_print("%s: pthread_create() failed to create Rx thread for device: [%s], errno: %s\n", __FUNCTION__, conf->host, strerror(errno));
			close(ctx->socket);
			free(ctx);
			return CSP_ERR_NOMEM;
		}
	}

	if (return_fd) {
		*return_fd = ctx->socket;
	}

	return CSP_ERR_NONE;
}
