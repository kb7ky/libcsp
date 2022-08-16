#pragma once

/**
   @file

   if_mqtt (Mqtt) interface.
L
   The mqtt interface is designed to connect to a mosquitto hub.

   For further details on mqtt, see http://www.Mqtt.org.
*/

#include <csp/csp_interface.h>

#define CSP_MQTT_MTU 2048  // max payload data, see documentation

/**
   mqttproxy default subscribe (rx) port.
   The client must connect it's publish endpoint to the mqttproxy's subscribe port.
*/
#define CSP_MQTT_SUBSCRIBE_PORT   6000

/**
   mqttproxy default publish (tx) port.
   The client must connect it's subscribe endpoint to the mqttproxy's publish port.
*/
#define CSP_MQTT_PUBLISH_PORT     7000

/**
   Default mqtt interface name.
*/
#define CSP_MQTTHUB_IF_NAME            "MQTT"

/**
   Setup mqtt interface. 
   @param[in] addr Source Address to be bound to the interface.
   @param[in] host HostIP of the broker
   @param[in] port HostPort of the broker
   @param[in] subscriberTopic Topic string used to match RX - /incoming
   @param[in] publisherTopic Topic string used to identify TX - /outgoing
   @param[in] user user for authentication
   @param[in] password password for authentication
   @param[in] encryptRx flag when true indicates encrypt received frames
   @param[in] encryptTx  flag when tru indicates encrypt transmitted packets
   @param[in] flipTopics reverse subscriberTopic and publisherTopic to enable back to back testing
   @param[in] aes256IV Crypto IV
   @param[in] aes256Key CryptoKey
   @param[out] return_interface created CSP interface.
   @return #CSP_ERR_NONE on succcess - else assert.
*/
int csp_mqtt_init(const uint16_t addr,
                     const char * ifname,
                     const char * host,
                     uint16_t port,
                     const char * subscriberTopic,
                     const char * publisherTopic,
                     const char * user,
                     const char * password,
                     int encryptRx,
                     int encryptTx,
                     int flipTopics,
                     const char * aes256IV,
                     const char * aes256Key,
                     csp_iface_t ** return_interface);

/**
   ControlPlane hook to set/get on/off encryption. 
   @param[in] if_name - name of the interface
   @param[in] txonoff - zero = off, non zero = on
   @param[in] rxonoff - zero = off, non zero = on
   @return #CSP_ERR_NONE on succcess - else assert.
*/
int csp_mqtt_setEncryption(char * if_name, int txonoff, int rxonoff);
int csp_mqtt_getEncryption(char * if_name, int *txonoff, int *rxonoff);

