

#include <csp/interfaces/csp_if_kiss.h>

#include <csp/csp_debug.h>
#include <stdlib.h>

#include <csp/csp.h>
#include <csp/drivers/tcp_kiss.h>

typedef struct {
	char name[CSP_IFLIST_NAME_MAX + 1];
	csp_iface_t iface;
	csp_kiss_interface_data_t ifdata;
	csp_tcp_fd_t fd;
} kiss_context_t;

static int kiss_driver_tx(void * driver_data, const unsigned char * data, size_t data_length) {

	kiss_context_t * ctx = driver_data;
	if (csp_tcp_write(ctx->fd, data, data_length) == (int)data_length) {
		return CSP_ERR_NONE;
	}
	ctx->fd = -1; // error has closed fd
	return CSP_ERR_TX;
}

static void kiss_driver_rx(void * user_data, uint8_t * data, size_t data_size, void * pxTaskWoken) {

	kiss_context_t * ctx = user_data;
	csp_kiss_rx(&ctx->iface, data, data_size, NULL);
}

static int kiss_driver_lock(void * driver_data) {
	csp_tcp_lock(driver_data);
	return CSP_ERR_NONE;
}

static int kiss_driver_unlock(void * driver_data) {
	csp_tcp_unlock(driver_data);
	return CSP_ERR_NONE;
}

int csp_tcp_open_and_add_kiss_interface(const csp_tcp_conf_t * conf, const char * ifname, csp_iface_t ** return_iface) {

	if (ifname == NULL) {
		ifname = CSP_IF_TCP_DEFAULT_NAME;
	}

	kiss_context_t * ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		return CSP_ERR_NOMEM;
	}

	strncpy(ctx->name, ifname, sizeof(ctx->name) - 1);
	ctx->iface.name = ctx->name;
	ctx->iface.driver_data = ctx;
	ctx->iface.interface_data = &ctx->ifdata;
	ctx->ifdata.tx_func = kiss_driver_tx;
	ctx->ifdata.lock_func = kiss_driver_lock;
	ctx->ifdata.unlock_func = kiss_driver_unlock;
#if (CSP_WINDOWS)
	ctx->fd = NULL;
#else
	ctx->fd = -1;
#endif

	int res = csp_kiss_add_interface(&ctx->iface);
	if (res == CSP_ERR_NONE) {
		res = csp_tcp_open(conf, kiss_driver_rx, ctx, &ctx->fd);
	}

	if (return_iface) {
		*return_iface = &ctx->iface;
	}

	return res;
}
