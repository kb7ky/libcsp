#pragma once

/**
   @file

   Socket CAN driver (Linux).

   This driver sends CAN pdu to a ECAN-240 tcp bridge device to get on the CAN bus
*/

#include <csp/csp_types.h>

/*
 * Copy from linux/can.h since windows doesn't support SOCKETCAN *
 */

/* Required typedefs for compile */
typedef uint32_t __u32;
typedef uint8_t __u8;

/* ************** note: from /usr/include/linux/can.h ***********/

/* controller area network (CAN) kernel definitions */

/* special address description flags for the CAN_ID */
#define CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U /* error message frame */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */

/*
 * Controller Area Network Identifier structure
 *
 * bit 0-28	: CAN identifier (11/29 bit)
 * bit 29	: error message frame flag (0 = data frame, 1 = error message)
 * bit 30	: remote transmission request flag (1 = rtr frame)
 * bit 31	: frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
 */
typedef __u32 canid_t;

#define CAN_SFF_ID_BITS		11
#define CAN_EFF_ID_BITS		29

/*
 * Controller Area Network Error Message Frame Mask structure
 *
 * bit 0-28	: error class mask (see include/uapi/linux/can/error.h)
 * bit 29-31	: set to zero
 */
typedef __u32 can_err_mask_t;

/* CAN payload length and DLC definitions according to ISO 11898-1 */
#define CAN_MAX_DLC 8
#define CAN_MAX_RAW_DLC 15
#define CAN_MAX_DLEN 8

/* CAN FD payload length and DLC definitions according to ISO 11898-7 */
#define CANFD_MAX_DLC 15
#define CANFD_MAX_DLEN 64

/**
 * struct can_frame - Classical CAN frame structure (aka CAN 2.0B)
 * @can_id:   CAN ID of the frame and CAN_*_FLAG flags, see canid_t definition
 * @len:      CAN frame payload length in byte (0 .. 8)
 * @can_dlc:  deprecated name for CAN frame payload length in byte (0 .. 8)
 * @__pad:    padding
 * @__res0:   reserved / padding
 * @len8_dlc: optional DLC value (9 .. 15) at 8 byte payload length
 *            len8_dlc contains values from 9 .. 15 when the payload length is
 *            8 bytes but the DLC value (see ISO 11898-1) is greater then 8.
 *            CAN_CTRLMODE_CC_LEN8_DLC flag has to be enabled in CAN driver.
 * @data:     CAN frame payload (up to 8 byte)
 */
struct can_frame {
	canid_t can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
	union {
		/* CAN frame payload length in byte (0 .. CAN_MAX_DLEN)
		 * was previously named can_dlc so we need to carry that
		 * name for legacy support
		 */
		__u8 len;
		__u8 can_dlc; /* deprecated */
	} __attribute__((packed)); /* disable padding added in some ABIs */
	__u8 __pad; /* padding */
	__u8 __res0; /* reserved / padding */
	__u8 len8_dlc; /* optional DLC for 8 byte payload length (9 .. 15) */
	__u8 data[CAN_MAX_DLEN] __attribute__((aligned(8)));
};

/*********************** end of /usr/include/linux/can.h **************/

#include <csp/interfaces/csp_if_can.h>

#define ECAN240_TCP_PORT 10003
#define ECAN240_CAN_PORT 1
#define CSP_TCPCAN_DEFAULT_NAME "TCAN"
#define CSP_HOSTNAME_MAX 2048

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

