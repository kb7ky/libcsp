#pragma once

/**
   @file

   Socket CAN driver (Linux).

   This driver sends CAN pdu to a PCAN udp bridge device to get on the CAN bus
*/

#include <csp/csp_types.h>
#include <csp/interfaces/csp_if_can.h>

#define PCAN_TX_UDP_PORT 58205
#define PCAN_RX_UDP_PORT 58204
#define PCAN_BUFF_SZ 1500

#define ECAN240_TCP_PORT 10003
#define ECAN240_CAN_PORT 1

#define CSP_TCPCAN_DEFAULT_NAME "TCAN"
#define CSP_HOSTNAME_MAX 2048

typedef struct {

	/* Should be set before calling init */
	char host[CSP_HOSTNAME_MAX];
    uint16_t tx_port;
    uint16_t rx_port;

	/* Internal parameters */

} csp_can_tcpcan_conf_t;

/* PCAN Header - peak systems */
typedef union _VALUE{
	int8_t		val8s[8];
	uint8_t		val8u[8];
	int16_t		val16s[4];
	uint16_t 	val16u[4];
	int32_t		val32s[2];
	uint32_t	val32u[2];
	int64_t		val64s;
	uint64_t	val64u;
}U_VALUE;

typedef struct _CAN2IP {
	uint16_t 	sz;
	uint16_t 	type;
	uint64_t 	tag;
	uint32_t 	timestamp64_lo;
	uint32_t 	timestamp64_hi;
	uint8_t 	channel;
	uint8_t 	dlc;
	uint16_t 	flag;
	uint32_t 	id;
	U_VALUE		data;
}__attribute__((__packed__)) S_CAN2IP;

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

