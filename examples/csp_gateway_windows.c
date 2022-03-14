#include <csp/csp.h>
#include <windows.h>
#include <process.h>

static int csp_win_thread_create(unsigned int (* routine)(void *)) {

	uintptr_t ret = _beginthreadex(NULL, 0, routine, NULL, 0, NULL);
	if (ret == 0) {
		return CSP_ERR_NOMEM;
	}

	return CSP_ERR_NONE;
}

static unsigned int task_gateway(void * param) {

	/* Here there be routing */
	while (1) {
		csp_route_work();
	}

	return 0;
}

int gateway_start(void) {
	return csp_win_thread_create(task_gateway);
}
