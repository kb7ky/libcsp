#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include <csp/csp_yaml.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>
#include <csp/csp_rtable.h>
#include <csp/csp_id.h>
#include <csp/interfaces/csp_if_zmqhub.h>
#include <csp/interfaces/csp_if_mqtt.h>
#include <csp/interfaces/csp_if_can.h>
#include <csp/interfaces/csp_if_lo.h>
#include <csp/interfaces/csp_if_tun.h>
#include <csp/interfaces/csp_if_udp.h>
#include <csp/drivers/can_socketcan.h>
#include <csp/drivers/can_tcpcan.h>
#include <csp/drivers/usart.h>
#include <csp/drivers/tcp_kiss.h>
#include <csp/csp_debug.h>
#include <yaml.h>

struct data_s {
	char * name;
	char * driver;
	char * device;
	char * addr;
	char * netmask;
	char * server;
	char * is_dfl;
	char * baudrate;
	char * source;
	char * destination;
	char * listen_port;
	char * remote_port;
	char * promisc;
	char * encryptRx;
	char * encryptTx;
	char * flipTopics;
	char * user;
	char * password;
	char * subscriberTopic;
	char * publisherTopic;
	char * aes256IV;
	char * aes256Key;
};

static int csp_yaml_getaddrinfo(char *fqdn, char *host, int hostsize) {
    struct addrinfo hints;
    struct addrinfo *res;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;

    int ret = getaddrinfo(fqdn, NULL, &hints, &res);
    if (ret != 0) {
        csp_print("csp_yaml_getaddrinfo failed: %d\n", ret);
        return(ret);
    }
    getnameinfo(res->ai_addr, res->ai_addrlen, host, hostsize, NULL, 0, NI_NUMERICHOST);
	freeaddrinfo(res);

	return 0;
}

static void csp_yaml_start_if(struct data_s * data) {
	memset(data, 0, sizeof(struct data_s));
}

static void csp_yaml_end_if(struct data_s * data, unsigned int * dfl_addr) {
	/* Sanity checks */
	if ((!data->name) || (!data->driver) || (!data->addr) || (!data->netmask)) {
		csp_print("  invalid interface found\n");
		return;
	}

	csp_iface_t * iface;

	int addr = atoi(data->addr);

	/* If dfl_addr is passed, we can either readout or override */
	if ((dfl_addr != NULL) && (data->is_dfl)) {
		if (*dfl_addr == 0) {
			*dfl_addr = addr;
		} else {
			addr = *dfl_addr;
		}
	}

	/* KISS/UART */
    if (strcmp(data->driver, "kiss") == 0) {

		/* Check for valid options */
		if (!data->baudrate) {
			csp_print("no baudrate configured\n");
			return;
		}

		csp_usart_conf_t conf = {
			.device = data->device,
			.baudrate = atoi(data->baudrate),
			.databits = 8,
			.stopbits = 1,
			.paritysetting = 0,
			.checkparity = 0
		};
		int error = csp_usart_open_and_add_kiss_interface(&conf, data->name, &iface);
		if (error != CSP_ERR_NONE) {
			return;
		}

	}

	/* KISS/TCP */
    if (strcmp(data->driver, "tcp") == 0) {

		if (!data->server || !data->remote_port) {
			csp_print("server or remote_port missing\n");
			return;
		}

		iface = calloc(1,sizeof(csp_iface_t));
		csp_tcp_conf_t * tcp_conf = calloc(1,sizeof(csp_tcp_conf_t));
		char addrBuffer[16]; // xxx.xxx.xxx.xxx\0
		if((csp_yaml_getaddrinfo(data->server, addrBuffer, sizeof(addrBuffer))) != 0) {
			csp_print("tcp: unable to resolve server name\n");
			exit(1);
		}
		tcp_conf->host = strdup(addrBuffer);
		tcp_conf->port = atoi(data->remote_port);
		tcp_conf->listen = (data->listen_port != 0);

		int error = csp_tcp_open_and_add_kiss_interface(tcp_conf, data->name, &iface);
		if (error != CSP_ERR_NONE) {
			return;
		}

	}

#if CSP_HAVE_IFTUN
	else if (strcmp(data->driver, "tun") == 0) {

		/* Check for valid options */
		if (!data->source || !data->destination) {
			csp_print("source or destination missing\n");
			return;
		}

		iface = calloc(1,sizeof(csp_iface_t));
		csp_if_tun_conf_t * ifconf = calloc(1,sizeof(csp_if_tun_conf_t));
		ifconf->tun_dst = atoi(data->destination);
		ifconf->tun_src = atoi(data->source);

		csp_if_tun_init(iface, ifconf);
	}
#endif
	else if (strcmp(data->driver, "udp") == 0) {

		/* Check for valid options */
		if (!data->server || !data->listen_port || !data->remote_port) {
			csp_print("server, listen_port or remote_port missing\n");
			return;
		}

		iface = calloc(1,sizeof(csp_iface_t));
		csp_if_udp_conf_t * udp_conf = calloc(1,sizeof(csp_if_udp_conf_t));
		// udp_conf->host = data->server;
		char addrBuffer[16]; // xxx.xxx.xxx.xxx\0
		if((csp_yaml_getaddrinfo(data->server, addrBuffer, sizeof(addrBuffer))) != 0) {
			csp_print("udp: unable to resolve server name\n");
			exit(1);
		}
		udp_conf->host = strdup(addrBuffer);
		udp_conf->lport = atoi(data->listen_port);
		udp_conf->rport = atoi(data->remote_port);
		csp_if_udp_init(iface, udp_conf);

	}

#if (CSP_HAVE_LIBZMQ)
	/* ZMQ */
    else if (strcmp(data->driver, "zmq") == 0) {
		

		/* Check for valid server */
		if (!data->server) {
			csp_print("no server configured\n");
			return;
		}

		char addrBuffer[16]; // xxx.xxx.xxx.xxx\0
		if((csp_yaml_getaddrinfo(data->server, addrBuffer, sizeof(addrBuffer))) != 0) {
			csp_print("zmq: unable to resolve server name\n");
			exit(1);
		}

		int promisc = 0;
		if (data->promisc) {
			promisc = (strcmp("true", data->promisc) == 0) ? 1 : 0;
		}

		csp_zmqhub_init_filter2(data->name, &addrBuffer[0], addr, atoi(data->netmask), promisc, 0, 0, &iface);
		// csp_zmqhub_init(addr, data->server, promisc, flags, portoffset, topiclen, &iface);

	}
#endif

#if (CSP_HAVE_LIBMQTT)
	/* ZMQ */
    else if (strcmp(data->driver, "mqtt") == 0) {
		

		/* Check for valid server */
		if (!data->server) {
			csp_print("no server configured\n");
			return;
		}

		char addrBuffer[16]; // xxx.xxx.xxx.xxx\0
		if((csp_yaml_getaddrinfo(data->server, addrBuffer, sizeof(addrBuffer))) != 0) {
			csp_print("mqtt: unable to resolve server name\n");
			exit(1);
		}

		int encryptRx = 0;
		if (data->encryptRx) {
			encryptRx = (strcmp("true", data->encryptRx) == 0) ? 1 : 0;
		}

		int encryptTx = 0;
		if (data->encryptTx) {
			encryptTx = (strcmp("true", data->encryptTx) == 0) ? 1 : 0;
		}

		int flipTopics = 0;
		if (data->flipTopics) {
			flipTopics = (strcmp("true", data->flipTopics) == 0) ? 1 : 0;
		}

		csp_print("AddrBuffer: %s\n", &addrBuffer[0]);
		csp_mqtt_init(addr, data->name, &addrBuffer[0], atoi(data->remote_port), data->subscriberTopic,
						data->publisherTopic, data->user, data->password, encryptRx, encryptTx, flipTopics,
						data->aes256IV, data->aes256Key, &iface);
	}
#endif

#if (CSP_HAVE_LIBSOCKETCAN)
	/* CAN */
	else if (strcmp(data->driver, "can") == 0) {

		/* Check for valid server */
		if (!data->device) {
			csp_print("can: no device configured\n");
			return;
		}

		int error = csp_can_socketcan_open_and_add_interface(data->device, data->name, 1000000, true, &iface);
		if (error != CSP_ERR_NONE) {
			csp_print("failed to add CAN interface [%s], error: %d", data->device, error);
			return;
		}

	}
#endif

	/* TCAN - CAN over TCP to ECAN-240 */
	else if (strcmp(data->driver, "tcan") == 0) {

		/* Check for valid server */
		if (!data->server || (!data->remote_port) || (!data->listen_port)) {
			csp_print("tcan: no host:tx_port/host:rx_port can port configured\n");
			return;
		}

		csp_can_tcpcan_conf_t  * tcpcan_conf = calloc(1,sizeof(csp_can_tcpcan_conf_t));
		char addrBuffer[16]; // xxx.xxx.xxx.xxx\0
		if((csp_yaml_getaddrinfo(data->server, addrBuffer, sizeof(addrBuffer))) != 0) {
			csp_print("tcpcan: unable to resolve server name\n");
			exit(1);
		}
		strncpy(tcpcan_conf->host, &addrBuffer[0], CSP_HOSTNAME_MAX);
		tcpcan_conf->tx_port = atoi(data->remote_port);
		tcpcan_conf->rx_port = atoi(data->listen_port);

		int error = csp_can_tcpcan_open_and_add_interface(data->name, tcpcan_conf, true, &iface);
		if (error != CSP_ERR_NONE) {
			csp_print("failed to add TCAN interface [%s], error: %d\n", data->name, error);
			return;
		}

	}

    /* Unsupported interface */
	else {
        csp_print("Unsupported driver %s\n", data->driver);
        return;
    }

	iface->addr = addr;
	iface->netmask = atoi(data->netmask);
	iface->name = strdup(data->name);

	// csp_print("csp_yaml -  %s addr: %u netmask %u\n", iface->name, iface->addr, iface->netmask);

}

static void csp_yaml_key_value(struct data_s * data, char * key, char * value) {

	if (strcmp(key, "name") == 0) {
		data->name = strdup(value);
	} else if (strcmp(key, "driver") == 0) {
		data->driver = strdup(value);
	} else if (strcmp(key, "device") == 0) {
		data->device = strdup(value);
	} else if (strcmp(key, "addr") == 0) {
		data->addr = strdup(value);
	} else if (strcmp(key, "netmask") == 0) {
		data->netmask = strdup(value);
	} else if (strcmp(key, "server") == 0) {
		data->server = strdup(value);
	} else if (strcmp(key, "default") == 0) {
		data->is_dfl = strdup(value);
	} else if (strcmp(key, "baudrate") == 0) {
		data->baudrate = strdup(value);
	} else if (strcmp(key, "source") == 0) {
		data->source = strdup(value);
	} else if (strcmp(key, "destination") == 0) {
		data->destination = strdup(value);
	} else if (strcmp(key, "listen_port") == 0) {
		data->listen_port = strdup(value);
	} else if (strcmp(key, "remote_port") == 0) {
		data->remote_port = strdup(value);
	} else if (strcmp(key, "promisc") == 0) {
		data->promisc = strdup(value);
	} else if (strcmp(key, "encryptRx") == 0) {
		data->encryptRx = strdup(value);
	} else if (strcmp(key, "encryptTx") == 0) {
		data->encryptTx = strdup(value);
	} else if (strcmp(key, "flipTopics") == 0) {
		data->flipTopics = strdup(value);
	} else if (strcmp(key, "user") == 0) {
		data->user = strdup(value);
	} else if (strcmp(key, "password") == 0) {
		data->password = strdup(value);
	} else if (strcmp(key, "subscriberTopic") == 0) {
		data->subscriberTopic = strdup(value);
	} else if (strcmp(key, "publisherTopic") == 0) {
		data->publisherTopic = strdup(value);
	} else if (strcmp(key, "aes256IV") == 0) {
		data->aes256IV = strdup(value);
	} else if (strcmp(key, "aes256Key") == 0) {
		data->aes256Key = strdup(value);
	} else {
		csp_print("Unknown key %s\n", key);
	}
}

void csp_yaml_init(char * filename, unsigned int * dfl_addr) {

    struct data_s data;

	csp_print("  Reading config from %s\n", filename);
	FILE * file = fopen(filename, "rb");
	if (file == NULL) {
		csp_print("Failed to open config file\n");
		return;
	}

	yaml_parser_t parser;
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, file);
	yaml_event_t event;

	yaml_parser_parse(&parser, &event);
	if (event.type != YAML_STREAM_START_EVENT) {
		yaml_event_delete(&event);
		yaml_parser_delete(&parser);
		return;
	}
	yaml_event_delete(&event);

	yaml_parser_parse(&parser, &event);
	if (event.type != YAML_DOCUMENT_START_EVENT) {
		yaml_event_delete(&event);
		yaml_parser_delete(&parser);
		return;
	}
	yaml_event_delete(&event);

	yaml_parser_parse(&parser, &event);
	if (event.type != YAML_SEQUENCE_START_EVENT) {
		yaml_event_delete(&event);
		yaml_parser_delete(&parser);
		return;
	}
	yaml_event_delete(&event);

	while (1) {

		yaml_parser_parse(&parser, &event);

		if (event.type == YAML_SEQUENCE_END_EVENT) {
			yaml_event_delete(&event);
			break;
		}

		if (event.type == YAML_MAPPING_START_EVENT) {
			csp_yaml_start_if(&data);
			yaml_event_delete(&event);
			continue;
		}

		if (event.type == YAML_MAPPING_END_EVENT) {
			csp_yaml_end_if(&data, dfl_addr);
			yaml_event_delete(&event);
			continue;
		}

		if (event.type == YAML_SCALAR_EVENT) {
			
			/* Got key, parse the value too */
			yaml_event_t event_val;
			yaml_parser_parse(&parser, &event_val);
			csp_yaml_key_value(&data, (char *) event.data.scalar.value, (char *) event_val.data.scalar.value);
			yaml_event_delete(&event_val);

			yaml_event_delete(&event);

			continue;
		}
	}

	/* Cleanup libyaml */
	yaml_parser_delete(&parser);

	/* Go through list of potentially allocated strings. Tedious cleanup */
	free(data.name);
	free(data.driver);
	free(data.device);
	free(data.addr);
	free(data.netmask);
	free(data.server);
	free(data.is_dfl);
	free(data.baudrate);
	free(data.source);
	free(data.destination);
	free(data.listen_port);
	free(data.remote_port);
	free(data.promisc);
	free(data.encryptRx);
	free(data.encryptTx);
	free(data.flipTopics);
	free(data.user);
	free(data.password);
	free(data.subscriberTopic);
	free(data.publisherTopic);
	free(data.aes256IV);
	free(data.aes256Key);

}
