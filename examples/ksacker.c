/*
 * sacker for KStuff
 */
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <csp/csp.h>
#include <endian.h>
#include <csp/csp_interface.h>
#include <csp/csp_id.h>
#include <stdlib.h>
#include <memory.h>
#include <sodium.h>
#include <netinet/in.h>

// fwd decls
void csp_t501_rx(csp_iface_t * iface, const uint8_t * buf, int len, void * pxTaskWoken);
int build_gt_frame(uint8_t * frame, int framelen, int payloadlen);
int build_nvp_frame(uint8_t * frame, int framelen, int payloadlen);
int send_frame(csp_iface_t * iface, uint8_t * frame, int framelen, int fragmentsize);

#define csp_hton32(x) htonl(x)
#define csp_ntoh32(x) ntohl(x)
#define csp_hton16(x) htons(x)
#define csp_ntoh16(x) ntohs(x)

#define SRS3_HDR_SIZE 2
#define SRS3_CRC32C_SIZE 4
#define RS_PARITY_SIZE 32
#define ASM_SIZE 4
#define ASM_VALUE 0x1acffc1d

/* defines (local) */
#define T501_CRYPTO_BUF_MAX 4096	// max size of crypto buffer - allows for massive expansion
#define T501_MAXPAYLOAD_SIZE 1024
#define SRS3_HDR_SIZE 2
#define SRS3_CRC32C_SIZE 4

// Generic Telecommand Interface - sent on TX port
typedef struct __attribute__((__packed__)) {
	unsigned int sync;
	unsigned int byte_len;
	unsigned int cmd_id;
	unsigned int bit_len;
	unsigned char frame_data[0];
} generic_telecommand_int_t;
#define GENERIC_TELECOMMAND_HDR_SIZE 16

// Set MACRO's
#define T501_SET_SYNC(x) csp_hton32(x)
#define T501_SET_BYTE_LEN(x) csp_hton32(x)
#define T501_SET_CMD_ID(x) csp_hton32(x)
#define T501_SET_BIT_LEN(x) csp_hton32(x)

// Get MACRO's
#define T501_GET_SYNC(x) csp_ntoh32(x)
#define T501_GET_BYTE_LEN(x) csp_ntoh32(x)
#define T501_GET_CMD_ID(x) csp_ntoh32(x)
#define T501_GET_BIT_LEN(x) csp_ntoh32(x)

// Command ACK Data Response - received on TX port
typedef struct __attribute__((__packed__)) {
	unsigned int sync;              // 0x52544C22 (ASCII “RTL” followed by 0x22)
	unsigned int byte_len;
	unsigned int cmd_id;
	unsigned int posix_sec;
	unsigned int posix_nano;
	unsigned int status;
} generic_telecommandack_int_t;
#define COMMAND_ACK_DATA_HDR_SIZE 24

// Generic Telemetry Interface - received on RX port
typedef struct __attribute__((__packed__)) {
	unsigned int ver_len;
	unsigned int posix_sec;
	unsigned int posix_nano;
	unsigned int flags;
	unsigned int bit_len;
	unsigned char frame_data[0];
} generic_telemetry_int_t;
#define GENERIC_TELEMETRY_HDR_SIZE 20
#define TELEMETRY_VERSION_MASK 0xf0000000
#define TELEMETRY_BYTE_LEN_MASK ~TELEMETRY_VERSION_MASK
#define TELEMETRY_VERSION 0x00

// Name Value Pair Data Header interface
typedef struct __attribute__((__packed__)) {
    char header[4];         // RTL4
    unsigned int byte_len;
    unsigned int metadata_items;
    char lenb[4];           // lenb
    unsigned int telemetry_len_bits;
    char LkSt[4];           // LkSt
    unsigned int lock_state;
    char TS_s[4];           // TS_s
    unsigned int timestamp_sec;
    char TSns[4];           // TSns
    unsigned int timestamp_nanosec;
    unsigned char frame_data[0];
} name_value_pair_int_t;
#define NAME_VALUE_PAIR_HDR_SIZE 44

// Lock_state definitions
#define LOCK_STATE_WAITING 0
#define LOCK_STATE_VERIFY 1
#define LOCK_STATE_LOCK 2
#define LOCK_STATE_CHECK 3

 /**
   T501 Rx mode/state.
*/
typedef enum {
    T501_MODE_START,                           //!< Initialize to start LOOKING_TYPE
    T501_MODE_LOOKING_TYPE,                    //!< RTL4 or version/byte count
    T501_MODE_LOOKING_NAME_VALUE,              //!< Got RTL4 - read in the rest of the header
	T501_MODE_LOOKING_GENERIC_TELEMETRY,       //!< Got version and Byte count - read in the rest of the header
    T501_MODE_LOOKING_DATA_FRAME,              //!< Processed header (RTL4 or VerBC) - read in data frame
    T501_MODE_DATA_COMPLETE_COMPLETE,          //!< have a full data frame frame - next is crc32/decrypt/convolution/etc
    T501_MODE_SKIP_FRAME,                      //!< lost sync or failed calloc
} csp_t501_mode_t;

/**
   T501 interface data (state information).
*/
typedef struct {
    /** Max Rx length */
    int payload_data_size;                          // set via control plane to frame_data_size in bytes (aka payload)
    csp_t501_mode_t rx_mode;                        // state variable

    /** Rx variables - controls input */
    unsigned int rx_length;                         // maximum we can receive as this is our buffer size
    unsigned char * rx_frame;                       // pointer to the frame buffer
    unsigned char * rx_frame_ptr;                   // pointer to where we are in the frame buffer

    /** Headers */
    union {                                             // input spot for header as we figure out which type
        char hdr_type_char[4];                          // spot for RTL4
        uint32_t hdr_type_uint;                         // spot for ver/byte count
        generic_telemetry_int_t hdr_gt;               // generic telemetry header
        name_value_pair_int_t hdr_nv;                   // Name/Value header
    } header;

    /** frame_data - payload to crc32, decrypt, etc... */
    unsigned char *frame_data;                      // malloc'ed frame data
    unsigned char *frame_data_ptr;                  // pointer into frame_data
    unsigned int frame_data_byte_len;               // byte length of framedata

    /** CSP packet data. */
    csp_packet_t * rx_packet;
} csp_t501_interface_data_t;

/*
 * main
 */
int main(int argc, char **argv) {
    // build up iface and ifdata
    csp_iface_t iface;
    csp_t501_interface_data_t ifdata;
    iface.interface_data = &ifdata;
    ifdata.payload_data_size = 1500;
    ifdata.rx_mode = T501_MODE_START;
    ifdata.rx_length = 0;

    int t501_frame_size = ifdata.payload_data_size + sizeof(generic_telemetry_int_t);
    uint8_t frame[t501_frame_size];
    int framelen = t501_frame_size;
    framelen = build_gt_frame(frame, framelen, ifdata.payload_data_size);

    printf("all at once\n");
    send_frame(&iface, frame, framelen, 0);

    printf("chunk of 3\n");
    send_frame(&iface, frame, framelen, 3);

    return 0;
}

/**
 * Decode received data and eventually route the packet.
 */
void csp_t501_rx(csp_iface_t * iface, const uint8_t * buf, int len, void * pxTaskWoken) {

	csp_t501_interface_data_t * ifdata = iface->interface_data;

	while (len-- > 0) {

        // printf("len %d\n",len);

		/* Input */
		uint8_t inputbyte = *buf++;

		switch (ifdata->rx_mode) {

            case T501_MODE_START:
                /* Start transfer */
                ifdata->rx_length = 4;
                ifdata->rx_mode = T501_MODE_LOOKING_TYPE;
                printf("START -> LOOKING_TYPE\n");
                ifdata->rx_frame = (unsigned char *) &ifdata->header.hdr_type_char;
                ifdata->rx_frame_ptr = ifdata->rx_frame;
                *ifdata->rx_frame_ptr++ = inputbyte;
                ifdata->rx_length--;
                break;
            case T501_MODE_LOOKING_TYPE:
                *ifdata->rx_frame_ptr++ = inputbyte;
                ifdata->rx_length--;
                if(ifdata->rx_length <= 0) {
                    /* we have the first uint32 - figure out the header type */
                    if(strncmp("RTL4", ifdata->header.hdr_type_char,4) == 0) {
                        /* RTL4 = Name/Value header */
                        ifdata->rx_mode = T501_MODE_LOOKING_NAME_VALUE;
                        printf("LOOKING_TYPE -> LOOKING_NAME_VALUE\n");
                        ifdata->rx_length = sizeof(name_value_pair_int_t) - 4;
                    } else {
                        /* Ver = 0 and byte_count = generic telemetry + current payload size */
                        /* Note this is sort of a hack, as the value could be matched with a random byte stream */
                        int byte_count = csp_ntoh32(ifdata->header.hdr_type_uint) & TELEMETRY_BYTE_LEN_MASK;
                        int ver = (csp_ntoh32(ifdata->header.hdr_type_uint) & TELEMETRY_VERSION_MASK) >> 4;
                        if((ver == TELEMETRY_VERSION) && (byte_count == (ifdata->payload_data_size + (int)sizeof(generic_telemetry_int_t)))) {
                            /* assumption is that this is a good generic telemetry header start */
                            ifdata->rx_mode = T501_MODE_LOOKING_GENERIC_TELEMETRY;
                            printf("LOOKING_TYPE -> T501_MODE_LOOKING_GENERIC_TELEMETRY\n");
                            ifdata->rx_length = sizeof(generic_telemetry_int_t) - 4;
                        } else {
                            /* not sure where we are in stream - resync */
                            ifdata->rx_mode = T501_MODE_START;
                        }
                    }
                }
                break;
            case T501_MODE_LOOKING_GENERIC_TELEMETRY:
                // pull in the generic telemetry header
                *ifdata->rx_frame_ptr++ = inputbyte;
                ifdata->rx_length--;

                if(ifdata->rx_length <= 0) {
                    // we have received the entire generic header - on to data
                    ifdata->rx_mode = T501_MODE_LOOKING_DATA_FRAME;
                    printf("T501_MODE_LOOKING_GENERIC_TELEMETRY -> T501_MODE_LOOKING_DATA_FRAME\n");

                    // XXX add check that compares bit count with byte count

                    // calloc the payload buffer
                    int byte_count = (csp_ntoh32(ifdata->header.hdr_type_uint) & TELEMETRY_BYTE_LEN_MASK) - sizeof(generic_telemetry_int_t);
                    printf("Data byte count %d\n", byte_count);
                    ifdata->rx_length = byte_count;
                    if((ifdata->rx_frame = calloc(1,byte_count)) == 0) {
                        /* skip this frame and hope this is temporary */
                        ifdata->rx_mode = T501_MODE_SKIP_FRAME;
                         printf("T501_MODE_LOOKING_GENERIC_TELEMETRY -> T501_MODE_SKIP_FRAME\n");
                       break;
                    }
                    ifdata->rx_frame_ptr = ifdata->rx_frame;
                }
                break;
            case T501_MODE_LOOKING_NAME_VALUE:
                // pull in the name value header
                *ifdata->rx_frame_ptr++ = inputbyte;
                ifdata->rx_length--;

                if(ifdata->rx_length <= 0) {
                    // we have received the entire generic header - on to data
                    ifdata->rx_mode = T501_MODE_LOOKING_DATA_FRAME;
                    printf("T501_MODE_LOOKING_NAME_VALUE -> T501_MODE_LOOKING_DATA_FRAME\n");

                    // calloc the payload buffer
                    int byte_count = csp_ntoh32(ifdata->header.hdr_nv.byte_len) - sizeof(name_value_pair_int_t);
                    ifdata->rx_length = byte_count;
                    if((ifdata->rx_frame = calloc(1,byte_count)) == 0) {
                        /* skip this frame and hope this is temporary */
                        ifdata->rx_mode = T501_MODE_SKIP_FRAME;
                        printf("T501_MODE_LOOKING_NAME_VALUE -> T501_MODE_SKIP_FRAME\n");
                        break;
                    }
                    ifdata->rx_frame_ptr = ifdata->rx_frame;
                }
                break;
            case T501_MODE_SKIP_FRAME:
                // we have lost sync or some other problen - skip until next frame
                ifdata->rx_length--;
                if(ifdata->rx_length <= 0) {
                    ifdata->rx_mode = T501_MODE_START;
                    printf("T501_MODE_SKIP_FRAME -> T501_MODE_START\n");
               }
                break;
            case T501_MODE_LOOKING_DATA_FRAME:
                *ifdata->rx_frame_ptr++ = inputbyte;
                ifdata->rx_length--;

                if(ifdata->rx_length <= 0) {

                    /*
                    crc32()
                    convolution()
                    decrypt()
                    end up with csp_packet
                    */

                    /* after finishing processing - free the payload buffer */
                    if(ifdata->rx_frame != 0) {
                        free(ifdata->rx_frame);
                        ifdata->rx_frame = 0;
                    }

                    /* all done sacking T501 - move to next steps */
                    ifdata->rx_mode = T501_MODE_START;
                    printf("T501_MODE_LOOKING_DATA_FRAME -> T501_MODE_START\n");
                }
                break;
            default:
                break;
        }

	}
}

int build_gt_frame(uint8_t *frame, int framelen, int payloadlen) {
    int i = 0;

    if((int)framelen < (GENERIC_TELEMETRY_HDR_SIZE + (int)payloadlen)) {
        return -1;
    }

    generic_telemetry_int_t * ptr = (generic_telemetry_int_t *)frame;
    ptr->ver_len = csp_hton32(framelen & TELEMETRY_BYTE_LEN_MASK);
    ptr->posix_sec = csp_hton32(0);
    ptr->posix_nano = csp_hton32(0);
    ptr->flags = csp_hton32(0);
    ptr->bit_len = csp_hton32(framelen * 8);

    // pattern data
    for(i=0; i < (int)payloadlen; i++) {
        ptr->frame_data[i] = (i & 0xff);
    }
    return (int)framelen;
}

int build_nvp_frame(uint8_t * frame, int framelen, int payloadlen) {
    int i = 0;

    if((int)framelen < (NAME_VALUE_PAIR_HDR_SIZE + (int)payloadlen)) {
        return -1;
    }

    name_value_pair_int_t * ptr = (name_value_pair_int_t *)frame;

    int byte_len = framelen, metadata_items = 4, telemetry_len_bits = payloadlen * 4, lock_state = LOCK_STATE_LOCK, timestamp_sec = 0, timestamp_nanosec = 0; 
    memcpy(ptr->header,"RTL4", 4);         // RTL4
    csp_hton32(byte_len);
    csp_hton32(metadata_items);
    memcpy(ptr->lenb, "lenb", 4);           // lenb
    csp_hton32(telemetry_len_bits);
    memcpy(ptr->LkSt, "LkSt", 4);           // LkSt
    csp_hton32(lock_state);
    memcpy(ptr->TS_s, "TS_s", 4);           // TS_s
    csp_hton32(timestamp_sec);
    memcpy(ptr->TSns, "TSns", 4);           // TSns
    csp_hton32(timestamp_nanosec);

    // pattern data
    for(i=0; i < (int)payloadlen; i++) {
        ptr->frame_data[i] = (i & 0xff);
    }
    return (int)framelen;

}

int send_frame(csp_iface_t * iface, uint8_t * frame, int framelen, int fragmentsize) {
    if(fragmentsize == 0) {
        // send the whole thing
        csp_t501_rx(iface, frame, framelen, NULL);
    } else {
        // break up frame into fragsize chunks for testing
        int len = framelen;
        uint8_t *ptr = frame;
        while(len > 0) {
            if(len < fragmentsize) {
                csp_t501_rx(iface, ptr, len, NULL);
            } else {
                csp_t501_rx(iface, ptr, fragmentsize, NULL);
            }
            ptr += fragmentsize;
            len -= fragmentsize;
        }
    }
    return CSP_ERR_NONE;
}
