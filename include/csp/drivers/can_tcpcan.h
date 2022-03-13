#pragma once

/**
   @file

   Socket CAN driver (Linux).

   This driver sends CAN pdu to a ECAN-240 tcp bridge device to get on the CAN bus
*/

#include <csp/interfaces/csp_if_can.h>

#define ECAN240_TCP_PORT 10003
#define ECAN240_CAN_PORT 1
#define CSP_TCPCAN_DEFAULT_NAME "TCAN"

typedef struct {

	/* Should be set before calling init */
	char * host;
   uint16_t ecan240_port;
	int canport;

	/* Internal parameters */

} csp_can_tcpcan_conf_t;

typedef struct {
   uint8_t canport;
   uint8_t flags;
} ECAN240HDR;

/**
   Open CAN socket and add CSP interface.

   @param[in] ifname CSP interface name, use #CSP_IF_CAN_DEFAULT_NAME for default name.
   @param[in] ifconf tcpcan interface config parameters (host and port)
   @param[in] promisc if \a true, receive all CAN frames. If \a false a filter is set on the CAN device, using device->addr
   @param[out] return_iface the added interface.
   @return The added interface, or NULL in case of failure.
*/
int csp_can_tcpcan_open_and_add_interface(const char * ifname, csp_can_tcpcan_conf_t * ifconf, bool promisc, csp_iface_t ** return_iface);

/**
   Stop the Rx thread and free resources (testing).

   @note This will invalidate CSP, because an interface can't be removed. This is primarily for testing.

   @param[in] iface interface to stop.
   @return #CSP_ERR_NONE on success, otherwise an error code.
*/
int csp_can_tcpcan_stop(csp_iface_t * iface);

