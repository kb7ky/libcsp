#include <inttypes.h>
#include <csp_autoconfig.h>

uint8_t csp_dbg_buffer_out;
uint8_t csp_dbg_errno;
uint8_t csp_dbg_conn_out;
uint8_t csp_dbg_conn_ovf;
uint8_t csp_dbg_conn_noroute;
uint8_t csp_dbg_can_errno;
uint8_t csp_dbg_inval_reply;
uint8_t csp_dbg_rdp_print;
uint8_t csp_dbg_packet_print;

#if (CSP_ENABLE_CSP_PRINT)
#if (CSP_PRINT_STDIO)
#include <stdarg.h>
#include <stdio.h>
__attribute__((weak)) void csp_print_func(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
#elif (CSP_PRINT_FILE)
#include <stdarg.h>
#include <stdio.h>
#define LOG_FILE "csp_output.log"
FILE *fileFp = NULL;
char csp_debug_print_buffer[2048];

__attribute__((weak)) void csp_print_func(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if(fileFp == NULL) {
        // not open yet
        fileFp = fopen(LOG_FILE,"w");
    }

    vsprintf(csp_debug_print_buffer, fmt, args);
    fprintf(fileFp, "%s", csp_debug_print_buffer);
    fflush(fileFp);

    va_end(args);
}
#else
__attribute__((weak)) void csp_print_func(const char * fmt, ...) {}
#endif
#endif
