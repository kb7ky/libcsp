#include <csp/csp_debug.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <csp/csp.h>
#include <csp/drivers/usart.h>
#include <csp/drivers/can_socketcan.h>
#include <csp/interfaces/csp_if_zmqhub.h>
#include <csp/interfaces/csp_if_lo.h>


/* These three functions must be provided in arch specific way */
int router_start(void);
int server_start(void);
int client_start(void);

/* Server port, the port the server listens on for incoming connections from the client. */
#define MY_SERVER_PORT		10
#define MY_SFP_SERVER_PORT  11

/* Commandline options */
static uint8_t server_address = 255;

/* test mode, used for verifying that host & client can exchange packets over the loopback interface */
static bool test_mode = false;
static unsigned int server_received = 0;
static unsigned int sfp_server_received = 0;
static int repeatCtr = -1;
static int sendSize = 100;
static bool fullSend = false;
static int clientFlags = CSP_O_NONE;
static int fastMode = 0;
static bool quietMode = false;
static bool pingSend = true;
static bool rebootSend = false;
static bool serverMode = false;
static int mtu = -1;

/* Server task - handles requests from clients */
void server(void) {

	csp_print("Server task started\n");

	/* Create socket with no specific socket options, e.g. accepts CRC32, HMAC, etc. if enabled during compilation */
	csp_socket_t sock = {0};
    
	/* Bind socket to all ports, e.g. all incoming connections will be handled here */
	csp_bind(&sock, CSP_ANY);

	/* Create a backlog of 10 connections, i.e. up to 10 new connections can be queued */
	csp_listen(&sock, 10);

	/* Wait for connections and then process packets on the connection */
	while (1) {

		/* Wait for a new connection, 10000 mS timeout */
		csp_conn_t *conn;
		if ((conn = csp_accept(&sock, 10000)) == NULL) {
			/* timeout */
			continue;
		}

		/* Read packets on connection, timout is 100 mS */
		csp_packet_t *packet;
        char *databuffer;
        int datasize = 0;
        bool more = true;
		while (more) {
			switch (csp_conn_dport(conn)) {
			case MY_SERVER_PORT:
                /* read packet here */
                packet = csp_read(conn, 50);
                more = false;

				/* Process packet here */
                if(!quietMode) {
				    csp_print("Normal Packet received on MY_SERVER_PORT: %s\n", (char *) packet->data);
                }
				csp_buffer_free(packet);
				++server_received;
				break;

			case MY_SFP_SERVER_PORT:
                /* read packet here */
                if(csp_sfp_recv(conn, (void **)&databuffer, &datasize, 50) != CSP_ERR_NONE) {
                    csp_print("Error reading SFP data\n");
                    more = false;
                    continue;
                }

				/* Process packet here */
                if(!quietMode) {
				    csp_print("SFP Packet received on MY_SFP_SERVER_PORT: size = %d\n", datasize);
                }
				free(databuffer);
				++sfp_server_received;
                more = false;
				break;

			default:
	            /* read packet here */
                packet = csp_read(conn, 50);
                more = false;

			    /* Call the default CSP service handler, handle pings, buffer use, etc. */
				csp_service_handler(packet);
				break;
			}
		}

		/* Close current connection */
		csp_close(conn);

	}

	return;

}
/* End of server task */

/* Client task sending requests to server task */
void client(void) {

	csp_print("Client task started");

	unsigned int count = 'A';
    int running = true;

	while (running == true) {

        if(repeatCtr != -1) {
            if(repeatCtr-- <= 0) {
                running = false;
                continue;
            }
        }

        if(fastMode == 0) {
		    usleep(test_mode ? 200000 : 1000000);
        } else {
            usleep(fastMode);
        }

        if(pingSend) {
            /* Send ping to server, timeout 1000 mS, ping size 100 bytes */
            int result = csp_ping(server_address, 1000, sendSize, clientFlags);
            if(!quietMode) {
                csp_print("Ping address: %u, result %d [mS]\n", server_address, result);
            }
            (void) result;
        }

        if(rebootSend) {
    		/* Send reboot request to server, the server has no actual implementation of csp_sys_reboot() and fails to reboot */
	    	csp_reboot(server_address);
            if(!quietMode) {
		        csp_print("reboot system request sent to address: %u\n", server_address);
            }
        }

        if(fullSend) {
		    /* Send data packet (string) to server */
		    /* 1. Connect to host on 'server_address', port MY_SERVER_PORT with regular UDP-like protocol and 1000 ms timeout */
		    csp_conn_t * conn = csp_connect(CSP_PRIO_NORM, server_address, MY_SERVER_PORT, 1000, clientFlags);
		    if (conn == NULL) {
			    /* Connect failed */
			    csp_print("Connection failed\n");
			    return;
		    }

		    /* 2. Get packet buffer for message/data */
		    csp_packet_t * packet = csp_buffer_get(100);
            if (csp_dbg_packet_print >= 1) {
                csp_print("Buffers left %d\n", csp_buffer_remaining());
            }
        
		    if (packet == NULL) {
			    /* Could not get buffer element */
			    csp_print("Failed to get CSP buffer\n");
			    return;
		    }

		    /* 3. Copy data to packet */
            memcpy(packet->data, "Hello world ", 12);
            memcpy(packet->data + 12, &count, 1);
            memset(packet->data + 13, 0, 1);
            count++;

		    /* 4. Set packet length */
		    packet->length = (strlen((char *) packet->data) + 1); /* include the 0 termination */

		    /* 5. Send packet */
		    csp_send(conn, packet);

		    /* 6. Close connection */
		    csp_close(conn);
        }

        if(mtu != -1) {
		    /* Send data packet using SFP to server */
		    /* 1. Connect to host on 'server_address', port MY_SERVER_PORT with regular UDP-like protocol and 1000 ms timeout */
		    csp_conn_t * conn = csp_connect(CSP_PRIO_NORM, server_address, MY_SFP_SERVER_PORT, 1000, clientFlags);
		    if (conn == NULL) {
			    /* Connect failed */
			    csp_print("Connection failed\n");
			    return;
		    }

		    /* 2. Get packet buffer for message/data */
		    char * databuffer;
            int datasize = mtu * 4;
            databuffer = calloc(1, datasize);
            if(databuffer == NULL) {
			    /* Could not get buffer for SFP */
			    csp_print("Failed to get SFP buffer\n");
			    return;
		    }

		    /* 3. Copy data to packet */

		    /* 4. Set packet length */

		    /* 5. Send packet */
		    csp_sfp_send(conn, databuffer, datasize, mtu,50);

		    /* 6. Close connection */
		    csp_close(conn);
        }
	}
    csp_iflist_print();

    exit(0);
	return;
}
/* End of client task */

/* main - initialization of CSP and start of server/client tasks */
int main(int argc, char * argv[]) {
    int portoffset = 0;
    uint8_t address = 0;
#if (CSP_HAVE_LIBSOCKETCAN)
    const char * can_device = NULL;
#endif
    const char * kiss_device = NULL;
#if (CSP_HAVE_LIBZMQ)
    const char * zmq_device = NULL;
#endif
    const char * rtable = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "a:dr:c:k:z:tR:hp:i:s:fCF:qSm:yo:M:")) != -1) {
        switch (opt) {
            case 'a':
                address = atoi(optarg);
                break;
			case 'd':
				csp_dbg_packet_print++;
				break;
            case 'r':
                server_address = atoi(optarg);
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
            case 't':
                test_mode = true;
                break;
            case 'R':
                rtable = optarg;
                break;
            case 'p':
                portoffset = atoi(optarg);
                break;
            case 'i':
                repeatCtr = atoi(optarg);
                break;
            case 's':
                sendSize = atoi(optarg);
                break;
            case 'f':
                fullSend = true;
                break;
            case 'C':
                clientFlags |= CSP_O_CRC32;
                break;
            case 'F':
                fastMode = atoi(optarg);
                break;
            case 'q':
                quietMode = true;
                break;
            case 'S':
                pingSend = false;
                rebootSend = false;
                break;
            case 'm':
                mtu = atoi(optarg);
                break;
            case 'y':
                clientFlags |= CSP_O_RDP;
               break;
            case 'o':
                csp_conf.pktsrc = (atoi(optarg) & 0x03);
                break;
            case 'M':
                csp_conf.mode = atoi(optarg);
                break;
            default:
                csp_print("Usage:\n"
                       " -a <address>     local CSP address\n"
                       " -d increment debug level, 0 - 6\n"
                       " -r <address>     run client against server address\n"
                       " -c <can-device>  add CAN device\n"
                       " -k <kiss-device> add KISS device (serial)\n"
                       " -z <zmq-device>  add ZMQ device, e.g. \"localhost\"\n"
                       " -R <rtable>      set routing table\n"
                       " -p <portoffset>  value to add to 6000/7000\n"
                       " -i <iteration count>  number of times to send - default forever\n"
                       " -s <size of probes/ping> size of data sent\n"
                       " -f full send of ping and Hello World data - otherwise just pings\n"
                       " -C use csp crc\n"
                       " -F <ms delay> between sends for client\n"
                       " -q quiet mode (minimal printing after startup\n"
                       " -S speed testing - turn off ping and reboot messages\n"
                       " -m <mtu> - use SFP protocol with MTU sized fragments\n"
                       " -y use rdp to ACK packets\n"
                       " -o packet Source ID\n"
                       " -M node Mode (0=none, 1=CmdTx, 2=TlmTx)\n"
                       " -t               enable test mode\n");
                exit(1);
                break;
        }
    }

    csp_print("Initialising CSP");

    /* set the lo address before calling csp_init() */
    csp_if_lo.addr = address;

    /* Init CSP */
    csp_init();

    /* Start router */
    router_start();

    /* Add interface(s) */
    csp_iface_t * default_iface = NULL;
    if (kiss_device) {
        csp_usart_conf_t conf = {
            .device = kiss_device,
            .baudrate = 115200, /* supported on all platforms */
            .databits = 8,
            .stopbits = 1,
            .paritysetting = 0,
            .checkparity = 0};
        int error = csp_usart_open_and_add_kiss_interface(&conf, CSP_IF_KISS_DEFAULT_NAME,  &default_iface);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add KISS interface [%s], error: %d\n", kiss_device, error);
            exit(1);
        }
    }
#if (CSP_HAVE_LIBSOCKETCAN)
    if (can_device) {
        int error = csp_can_socketcan_open_and_add_interface(can_device, CSP_IF_CAN_DEFAULT_NAME, 0, false, &default_iface);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add CAN interface [%s], error: %d\n", can_device, error);
            exit(1);
        }
    }
#endif
#if (CSP_HAVE_LIBZMQ)
    if (zmq_device) {
        uint32_t flags = 0;
        int topiclen = 0;
        int error = csp_zmqhub_init(0, zmq_device, flags, portoffset, topiclen, &default_iface);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add ZMQ interface [%s], error: %d\n", zmq_device, error);
            exit(1);
        }
    }
#endif

    if (rtable) {
        int error = csp_rtable_load(rtable);
        if (error < 1) {
            csp_print("csp_rtable_load(%s) failed, error: %d\n", rtable, error);
            exit(1);
        }
    } else if (default_iface) {
	    default_iface->addr = address;
        csp_rtable_set(0, 0, default_iface, CSP_NO_VIA_ADDRESS);
    } else {
        /* no interfaces configured - run server and client in process, using loopback interface */
        server_address = address;
    }

    csp_print("Connection table\r\n");
    csp_conn_print_table();

    csp_print("Interfaces\r\n");
    csp_rtable_print();

    csp_print("Route table\r\n");
    csp_iflist_print();

    /* Start server thread */
    if ((server_address == 255) ||  /* no server address specified, I must be server */
        (default_iface == NULL)) {  /* no interfaces specified -> run server & client via loopback */
        serverMode = true;
        server_start();
    }

    /* Start client thread */
    if ((server_address != 255) ||  /* server address specified, I must be client */
        (default_iface == NULL)) {  /* no interfaces specified -> run server & client via loopback */
        serverMode = true;
        server_start();
        client_start();
    }

    /* Wait for execution to end (ctrl+c) */
    while(1) {

        if (test_mode) {
            sleep(3);
            /* Test mode is intended for checking that host & client can exchange packets over loopback */
            if (server_received < 5) {
                csp_print("Server received %u packets\n", server_received);
                exit(1);
            }
            csp_print("Server received %u packets\n", server_received);
            exit(0);
        }
        sleep(60);
        if(serverMode) {
            csp_iflist_print();
            csp_iflist_reset();
        }
    }

    return 0;
}
