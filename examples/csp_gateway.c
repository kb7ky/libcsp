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
char yamlfn[CSP_MAX_STRING + 10];

/* forward Decls */
int gateway_start(void);

/* main - initialization of CSP and start of bridge tasks */
int main(int argc, char * argv[]) {

    int opt;

    while ((opt = getopt(argc, argv, "h:dm:r:v:")) != -1) {
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
            default:
                csp_print("Usage:\n"
                       " -d increment packet debug level\n"
                       " -h <hostname> also used to open <hostname>.yaml\n"
                       " -m <model\n"
                       " -r <revision\n"
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
    snprintf(yamlfn, CSP_MAX_STRING + 10, "yaml/%s.yaml", hostname);

    csp_print("Initializing CSP Gateway\n");

    /* Init CSP */
    csp_init();
    csp_yaml_init(yamlfn,NULL);
    csp_print("CSP Bridge - v %d %s %s %s\n",csp_conf.version, csp_conf.hostname, csp_conf.model, csp_conf.revision);

    /* Start router */
    gateway_start();

    csp_print("\n\nInterfaces\r\n");
    csp_iflist_print();

    /* Wait for execution to end (ctrl+c) */
    while(1) {
        sleep(3);
    }

    return 0;
}

