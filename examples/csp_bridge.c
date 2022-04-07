#include <csp/csp_debug.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <csp/csp.h>
#include <csp/csp_yaml.h>
#include <csp/csp_iflist.h>
#include <csp/interfaces/csp_if_lo.h>

// used to plug in config
extern csp_conf_t csp_conf;

// buffers for strings
#define CSP_MAX_STRING 48
char hostname[CSP_MAX_STRING];
char model[CSP_MAX_STRING];
char revision[CSP_MAX_STRING];
char yamlfn[CSP_MAX_STRING * 3];

/* forward Decls */
int bridge_start(void);

/* main - initialization of CSP and start of bridge tasks */
int main(int argc, char * argv[]) {

    int opt;
    char defaultYamlPath[] = ".";
    char yamlBuffer[CSP_MAX_STRING + 1];
    char * yamlpathPtr = &defaultYamlPath[0];

    while ((opt = getopt(argc, argv, "h:dm:r:v:p:")) != -1) {
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
            default:
                csp_print("Usage:\n"
                       " -d increment packet debug level\n"
                       " -h <hostname> also used to open <hostname>.yaml\n"
                       " -m <model\n"
                       " -r <revision\n"
                       " -p path to directory holding yaml file\n"
                       " -v <csp version (1/2)\n"
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

    /* Wait for execution to end (ctrl+c) */
    while(1) {
        sleep(60);
        csp_iflist_print();
    }

    return 0;
}

