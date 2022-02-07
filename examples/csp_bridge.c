#include <csp/csp_debug.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <csp/csp.h>
#include <csp/drivers/usart.h>
#include <csp/drivers/can_socketcan.h>
#include <csp/interfaces/csp_if_zmqhub.h>
#include <csp/interfaces/csp_if_udp.h>

/* Hard coded UDP Ports for now */
#define UDP_LPORT 6006
#define UDP_RPORT 6007

/* forward Decls */
int bridge_start(void);

/* main - initialization of CSP and start of bridge tasks */
int main(int argc, char * argv[]) {

#if (CSP_HAVE_LIBSOCKETCAN)
    const char * can_device = NULL;
#endif
    const char * kiss_device = NULL;
#if (CSP_HAVE_LIBZMQ)
    const char * zmq_device = NULL;
#endif
    char * udp_device = NULL;

    int opt;

    while ((opt = getopt(argc, argv, "c:d:k:z:u:h")) != -1) {
        switch (opt) {
			case 'd':
				csp_dbg_packet_print++;
				break;
#if (CSP_HAVE_LIBSOCKETCAN)
            case 'c':
                can_device = optarg;
                break;
#endif
            case 'k':
                kiss_device = optarg;
                break;
#if (CSP_HAVE_LIBZMQ)
            case 'z':
                zmq_device = optarg;
                break;
#endif
	    case 'u':
		udp_device = optarg;
		break;
           default:
                csp_print("Usage:\n"
                       " -d <debug-level> packet debug level\n"
                       " -c <can-device>  add CAN device\n"
                       " -k <kiss-device> add KISS device (serial)\n"
                       " -z <zmq-device>  add ZMQ device, e.g. \"localhost\"\n"
		       " -u <udp-device>  add UDP device, e.g. \"localhost\"\n"
		);
                exit(1);
                break;
        }
    }

    csp_print("Initializing CSP Bridge");

    /* Init CSP */
    csp_init();

    /* Start router */
    bridge_start();

    /* Add interfaces - 2 required */
    csp_iface_t * bridge_iface[2] = { NULL, NULL };
	int bridge_iface_idx = 0;

    if (kiss_device) {
        csp_usart_conf_t conf = {
            .device = kiss_device,
            .baudrate = 115200, /* supported on all platforms */
            .databits = 8,
            .stopbits = 1,
            .paritysetting = 0,
            .checkparity = 0};
        int error = csp_usart_open_and_add_kiss_interface(&conf, CSP_IF_KISS_DEFAULT_NAME,  &bridge_iface[bridge_iface_idx]);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add KISS interface [%s], error: %d\n", kiss_device, error);
            exit(1);
        }
		bridge_iface_idx++;
		if(bridge_iface_idx > 1) {
			csp_print("Too many bridge interfaces specified\n");
			exit(2);
		}
    }
#if (CSP_HAVE_LIBSOCKETCAN)
    if (can_device) {
        int error = csp_can_socketcan_open_and_add_interface(can_device, CSP_IF_CAN_DEFAULT_NAME, 0, false, &bridge_iface[bridge_iface_idx]);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add CAN interface [%s], error: %d\n", can_device, error);
            exit(1);
        }
		bridge_iface_idx++;
		if(bridge_iface_idx > 1) {
			csp_print("Too many bridge interfaces specified\n");
			exit(2);
		}
     }
#endif
#if (CSP_HAVE_LIBZMQ)
    if (zmq_device) {
        int error = csp_zmqhub_init(0, zmq_device, 0, &bridge_iface[bridge_iface_idx]);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add ZMQ interface [%s], error: %d\n", zmq_device, error);
            exit(1);
        }
 		bridge_iface_idx++;
		if(bridge_iface_idx > 1) {
			csp_print("Too many bridge interfaces specified\n");
			exit(2);
		}
    }
#endif
    if (udp_device) {
		csp_if_udp_conf_t udp_ifconf;
		csp_iface_t udp_iface;
		bridge_iface[bridge_iface_idx] = &udp_iface;
		udp_ifconf.host = udp_device;
		udp_ifconf.lport = UDP_LPORT;
		udp_ifconf.rport = UDP_RPORT;
		
        	csp_if_udp_init(bridge_iface[bridge_iface_idx], &udp_ifconf);
 		bridge_iface_idx++;
		if(bridge_iface_idx > 1) {
			csp_print("Too many bridge interfaces specified\n");
			exit(2);
		}
    }

	csp_bridge_set_interfaces(bridge_iface[0], bridge_iface[1]);
	csp_print("Interfaces\r\n");

    /* Wait for execution to end (ctrl+c) */
    while(1) {
        sleep(3);
    }

    return 0;
}

