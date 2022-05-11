#pragma once

/**
   @file

   TCP/KISS driver.

   @note This interface implementation only support ONE open TCP connection.
*/

#include <csp/interfaces/csp_if_kiss.h>

#if (CSP_WINDOWS)
#include <windows.h>
#endif

#define CSP_IF_TCP_DEFAULT_NAME "TCPKISS"


/**
   OS file handle.
*/
#if (CSP_WINDOWS)
    typedef HANDLE csp_tcp_fd_t;
#else
    typedef int csp_tcp_fd_t;
#endif

/**
   TCP configuration.
   @see csp_tcp_open()
*/
typedef struct csp_tcp_conf {
    char *host;
    uint16_t port;
    int listen;
} csp_tcp_conf_t;

/**
   Callback for returning data to application.

   @param[in] buf data received.
   @param[in] len data length (number of bytes in \a buf).
   @param[out] pxTaskWoken Valid reference if called from ISR, otherwise NULL!
*/
typedef void (*csp_tcp_callback_t) (void * user_data, uint8_t *buf, size_t len, void *pxTaskWoken);

/**
   Opens an TCP connection.

   Opens the TCP connection and creates a thread for reading/returning data to the application.

   @note On read failure, exit() will be called - terminating the process.

   @param[in] conf UART configuration.
   @param[in] rx_callback receive data callback.
   @param[in] user_data reference forwarded to the \a rx_callback function.
   @param[out] fd the opened file descriptor.
   @return #CSP_ERR_NONE on success, otherwise an error code.
*/
int csp_tcp_open(const csp_tcp_conf_t *conf, csp_tcp_callback_t rx_callback, void * user_data, csp_tcp_fd_t * fd);

/**
 * Write data on open TCP connection.
 * @return number of bytes written on success, a negative value on failure. 
 */
int csp_tcp_write(csp_tcp_fd_t fd, const void * data, size_t data_length);

/**
 * Lock the device, so only a single user can write to the socket at a time
 */
void csp_tcp_lock(void * driver_data);

/**
 * Unlock the TCP socket again
 */
void csp_tcp_unlock(void * driver_data);

/**
   Opens TCP connection and add KISS interface.

   This is a convience function for opening an TCP connection and adding it as an interface with a given name.

   @note On read failures, exit() will be called - terminating the process.

   @param[in] conf TCP configuration.
   @param[in] ifname internface name (will be copied), or use NULL for default name.
   @param[out] return_iface the added interface.
   @return #CSP_ERR_NONE on success, otherwise an error code.
*/
int csp_tcp_open_and_add_kiss_interface(const csp_tcp_conf_t *conf, const char * ifname, csp_iface_t ** return_iface);
