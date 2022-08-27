#include <csp/csp_debug.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <zmq.h>
#include <assert.h>
#include <malloc.h>

#include <csp/csp.h>
#include <csp/csp_yaml.h>
#include <csp/csp_iflist.h>
#include <csp/interfaces/csp_if_lo.h>
#include <csp/interfaces/csp_if_mqtt.h>

// used to plug in config
extern csp_conf_t csp_conf;

// defines
#define CONTROLPLANE_BASE_PORT 6000

// control plane
typedef struct {
	void * context;
	void * publisher;
	void * subscriber;
    char * publish_endpoint;
    char * subscribe_endpoint;
} cpdata_t;

cpdata_t cpdata;
cpdata_t *cpd = &cpdata;

// buffers for strings
#define CSP_MAX_STRING 48
char hostname[CSP_MAX_STRING];
char model[CSP_MAX_STRING];
char revision[CSP_MAX_STRING];
char yamlfn[CSP_MAX_STRING * 3];
char * cpPublisherBuf;
char * cpSubscriberBuf;
char controlPlaneHost[CSP_MAX_STRING];
int  controlPlaneHostPubPort = 0;
int  controlPlaneHostSubPort = 0;
int controlPlaneActive = 1;
zmq_pollitem_t items [1];

/* forward Decls */
int bridge_start(void);
int makeEndpoints(char * buf, int buf_size, char *host, int port);
int controlPlaneInit(char * publish_endpoint, char * subscribe_endpoint);
void controlPlaneMessageProcess(uint8_t *inData);

/* main - initialization of CSP and start of bridge tasks */
int main(int argc, char * argv[]) {

    int opt;
    int rc = 0;
    char defaultYamlPath[] = ".";
    char defaultControlPlaneHost[] = "controlplane";
    char *cpHost = &defaultControlPlaneHost[0];
    char yamlBuffer[CSP_MAX_STRING + 1];
    char * yamlpathPtr = &defaultYamlPath[0];

    while ((opt = getopt(argc, argv, "h:dm:r:v:p:o:M:c:P:S:")) != -1) {
        switch (opt) {
			case 'd':
				csp_dbg_packet_print++;
				break;
            case 'h':
                strncpy(hostname, optarg, CSP_MAX_STRING);
                csp_conf.hostname = hostname;
                break;
            case 'm':
                strncpy(model, optarg, CSP_MAX_STRING);
                csp_conf.model = model;
                break;
            case 'r':
                strncpy(revision, optarg, CSP_MAX_STRING);
                csp_conf.revision = revision;
                break;
            case 'v':
                csp_conf.version = atoi(optarg);
                break;
            case 'p':
                strncpy(yamlBuffer, optarg, CSP_MAX_STRING);
                yamlpathPtr = yamlBuffer;
                break;
            case 'o':
                csp_conf.pktsrc = (atoi(optarg) & 0x03);
                break;
            case 'M':
                csp_conf.mode = atoi(optarg);
                break;
            case 'c':
                strncpy(controlPlaneHost, optarg, CSP_MAX_STRING);
                cpHost = controlPlaneHost;
                break;
            case 'P':
                controlPlaneHostPubPort = atoi(optarg);
                break;
            case 'S':
                controlPlaneHostSubPort = atoi(optarg);
                break;
            default:
                csp_print("Usage:\n"
                       " -d increment packet debug level\n"
                       " -h <hostname> also used to open <hostname>.yaml\n"
                       " -m <model\n"
                       " -r <revision\n"
                       " -p path to directory holding yaml file\n"
                       " -o packet Source ID\n"
                       " -M node Mode (0=none, 1=CmdTx, 2=TlmTx)\n"
                       " -v <csp version (1/2)\n"
                       " -c controlPlaneHostname\n"
                       " -P controlPlaneHostPubPort\n"
                       " -S controlPlaneHostSubPort\n"
            		);
                exit(1);
                break;
        }
    }

    if(strlen(hostname) == 0) {
        csp_print("Missing Hostname - can't open yaml file\n");
        exit(1);
    }
    snprintf(yamlfn, CSP_MAX_STRING * 3, "%s/yaml/%s.yaml", yamlpathPtr, hostname);

    csp_print("Initializing CSP Bridge\n");

    /* Init CSP */
    csp_init();
    csp_yaml_init(yamlfn,NULL);
    csp_print("CSP Bridge - v %d %s %s %s\n",csp_conf.version, csp_conf.hostname, csp_conf.model, csp_conf.revision);

    /* Start router */
    bridge_start();

    /* Add interfaces - 2 required */
    csp_iface_t * bridge_iface[2] = { NULL, NULL };
    int bridge_idx = 0;

    // fetch interfaces setup in yaml - must be 2
	csp_iface_t * ifc = csp_iflist_get();
	while (ifc) {
        // skip loopback
        if (strncmp(ifc->name, CSP_IF_LOOPBACK_NAME, CSP_IFLIST_NAME_MAX) != 0) {
            bridge_iface[bridge_idx] = ifc;
            bridge_idx++;
        }
		ifc = ifc->next;
	}

    if(bridge_idx != 2) {
        csp_print("Incorrect number of interfaces -  2 required to bridge\n");
        exit(2);
    }

	csp_bridge_set_interfaces(bridge_iface[0], bridge_iface[1]);
	csp_print("\n\nInterfaces\r\n");
    csp_iflist_print();

    /* setup the control plane endpoints */
    if(controlPlaneHostPubPort == 0 || controlPlaneHostSubPort == 0) {
        controlPlaneActive = 0;
    }

    if(controlPlaneActive) {
        int buf_size = CSP_MAX_STRING;
        if((cpPublisherBuf =  calloc(buf_size, 1)) == NULL) {
            csp_print("Failed to calloc for Publisher Buffer\n");
            exit(3);
        }
        if(makeEndpoints(cpPublisherBuf, buf_size, cpHost, controlPlaneHostPubPort) != CSP_ERR_NONE) {
            csp_print("Failed to build Pub Endpoint\n");
            exit(4);
        }
        if((cpSubscriberBuf =  calloc(buf_size, 1)) == NULL) {
            csp_print("Failed to calloc for Subscriber Buffer\n");
            exit(5);
        }
        if(makeEndpoints(cpSubscriberBuf, buf_size, cpHost, controlPlaneHostSubPort) != CSP_ERR_NONE) {
            csp_print("Failed to build Sub Endpoint\n");
            exit(6);
        }

        /* initialize Control Place ZMQ */
        csp_print("Initializing ControlPlane for Bridge\n");
        if(controlPlaneInit(cpPublisherBuf, cpSubscriberBuf) != CSP_ERR_NONE) {
            csp_print("Failed to initialize Control Plane ZMQ\n");
            exit(7);
        }   

    }

    /* Wait for execution to end (ctrl+c) */
    while(1) {
        if(controlPlaneActive) {
		    zmq_msg_t msg;
	        uint8_t *rx_data;

            items[0].socket = cpd->subscriber;
            items[0].events = ZMQ_POLLIN;

            rc = zmq_poll (items, 1, 60 * 1000); // 60 sec
            if(rc > 0) {
		        zmq_msg_init_size(&msg, 1024);
                if (zmq_msg_recv(&msg, cpd->subscriber, 0) < 0) {
                    zmq_msg_close(&msg);
                    csp_print("ZMQ: control plane error %s\n", zmq_strerror(zmq_errno()));
                    continue;
                }
		        int datalen = zmq_msg_size(&msg);
			    rx_data = zmq_msg_data(&msg);
			    rx_data[datalen] = '\0';
			    csp_print(">%s<\n", rx_data);

                csp_print("Processing ControlPlane Message\n");
                controlPlaneMessageProcess(rx_data);

                zmq_msg_close(&msg);
            }
        } else {
            sleep(60);
        }

        csp_iflist_print();
        csp_iflist_reset();
    }

    return 0;
}

int makeEndpoints(char * buf, int buf_size, char *host, int port) {
    int res = snprintf(buf, buf_size, "tcp://%s:%u", host, port);
	if ((res < 0) || (res >= (int)buf_size)) {
		buf[0] = 0;
		return CSP_ERR_NOMEM;
	}
	return CSP_ERR_NONE;
}


int controlPlaneInit(char * publish_endpoint, char * subscribe_endpoint) {
    int ret = 0;

    cpd->context = zmq_ctx_new();
	assert(cpd->context != NULL);

	csp_print("ZMQ CONTROLPLANE INIT BRIDGE: pub(tx): [%s] - sub(rx): [%s]\n", publish_endpoint, subscribe_endpoint);

    /* save endpoints for retries */
    cpd->publish_endpoint = publish_endpoint;
    cpd->subscribe_endpoint = subscribe_endpoint;

	/* Publisher (TX) */
	cpd->publisher = zmq_socket(cpd->context, ZMQ_PUB);
	assert(cpd->publisher != NULL);

	/* Subscriber (RX) */
	cpd->subscriber = zmq_socket(cpd->context, ZMQ_SUB);
	assert(cpd->subscriber != NULL);

	// XXX Debug - subscribe to all packets - no filter
    char order[] = "ORDER|BRIDGE1|MQTT";
    char req[] = "REQ|BRIDGE1|MQTT";
	ret = zmq_setsockopt(cpd->subscriber, ZMQ_SUBSCRIBE, order, strlen(order));
	assert(ret == 0);
    ret = zmq_setsockopt(cpd->subscriber, ZMQ_SUBSCRIBE, req, strlen(req));
	assert(ret == 0);

	/* setup HEARTBEAT on the connections */
	int hb_interval = 2000;
	int hb_timeout = 5000;
	int hb_remote_ttl = 7000;
	assert(zmq_setsockopt(cpd->publisher, ZMQ_HEARTBEAT_IVL, &hb_interval, sizeof(int)) == 0);
	assert(zmq_setsockopt(cpd->publisher, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(int)) == 0);
	assert(zmq_setsockopt(cpd->publisher, ZMQ_HEARTBEAT_TTL, &hb_remote_ttl, sizeof(int)) == 0);

	/* Connect to server */
	ret = zmq_connect(cpd->publisher, publish_endpoint);
	assert(ret == 0);
	zmq_connect(cpd->subscriber, subscribe_endpoint);
	assert(ret == 0);

    return CSP_ERR_NONE;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
void controlPlaneMessageProcess(uint8_t *inData) {
    char *token = NULL;
    int idx = 0;
    int res = CSP_ERR_NONE;

    /* storage for recv'd message processing - on stack so must zero */
    char task[256];
    memset(task, 0, 256);
    char targetGs[256];
    memset(targetGs, 0, 256);
    char targetRfLink[256];
    memset(targetRfLink, 0, 256);
    int id = 0;
    char payload[1024];
    memset(payload, 0, 1024);

    token = strtok((char *) inData, "|");

    while(token) {
        switch(idx) {
            case 0:
                // Task
                strncpy(task, token, 255);
                break;
            case 1:
                // TargetGS
                strncpy(targetGs, token, 255);
                break;
            case 2:
                // TargetRfLink
                strncpy(targetRfLink, token, 255);
                break;
            case 3:
                // ID
                id = atoi(token);
                break;
            case 4:
                // Payload
                strncpy(payload, token, 1023);
                break;
            default:
                csp_print("controlPlaneMessageProcess overflow idx %d\n",idx);
                break;
        }
        idx++;
        token = strtok(NULL,"|");
    }
    csp_print("CP Recv:\n  task: %s\n  targetGs: %s\n  targetRfLink: %s\n  id: %d\n  payload: %s\n\n",
                task, targetGs, targetRfLink, id, payload);
    
    /* explode payload and process */
    char cmd[256];
    memset(cmd, 0, 256);
    int encryptTx = 0;
    int encryptRx = 0;

    token = strtok(payload, " ");
    idx = 0;
    while(token) {
        switch(idx) {
            case 0:
                strncpy(cmd, token, 255);
                break;
            case 1:
                encryptTx = atoi(token);
                break;
            case 2:
                 encryptRx = atoi(token);
               break;
            default:
                break;
        }
        idx++;
        token = strtok(NULL, " ");
    }

    if(strncmp(cmd, "ENCRYPT", 255) == 0) {
        res = csp_mqtt_setEncryption(targetRfLink, encryptTx, encryptRx);
        if(res != CSP_ERR_NONE) {
            csp_print("CP Recv: setEncryption failed - err %d", res);
        }
    }
}
#pragma GCC diagnostic pop
