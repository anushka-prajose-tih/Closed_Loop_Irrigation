#ifndef GSM_EC200U_H_
#define GSM_EC200U_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Upload transport for the flash records:
     0 - publish on the MQTT "data" topic, and keep the MQTT link up for
         motor_cmd/cmd/ota handling
     1 - HTTP POST each record to SERVER_URL; the MQTT link, command
         handling and motor timer are compiled out
   Always defined and tested with #if (USE_HTTP_UPLOAD == x), per MISRA C
   Rules 20.8/20.9. A build configuration can override it with
   -DUSE_HTTP_UPLOAD=0 or =1. */
#ifndef USE_HTTP_UPLOAD
#define USE_HTTP_UPLOAD 1
#endif
#if (USE_HTTP_UPLOAD != 0) && (USE_HTTP_UPLOAD != 1)
#error "USE_HTTP_UPLOAD must be 0 (MQTT) or 1 (HTTP)"
#endif

#include "main.h"


#define TIME_SYNC_URL   "http://www.tih.iitb.ac.in/"
#if (USE_HTTP_UPLOAD == 1)
//Current URL is test URL for agriOS project.
#define SERVER_URL      "https://eagris.tihiitb.org/eagris/v1/sensor-data/public/receive/data"     //Active URL : "http://15.206.109.220/dev/v1/ingest"
#endif
/* Broker as in mqtt_device.h (EC200 MQTT device): public, port 1883, no login
   (GSM_MQTT_connect() sends no username/password) */
#define MQTT_HOST       "broker.hivemq.com"    // was "test.mosquitto.org", earlier "mqtt.tihiitb.org"
#define MQTT_SSL_HOST   "mqtt.tihiitb.org"
#define MQTT_PORT       "1883"
/* Mobile networks drop idle TCP connections, often within a minute, so the
   MQTT keepalive must be shorter than the modem's default */
#define MQTT_KEEPALIVE_S        30U
/* Public DNS for the data connection: the network's own resolver has failed
   to look up the broker (+QMTOPEN: 0,4) on the Airtel IoT SIM */
#define GSM_DNS_SERVERS         "\"8.8.8.8\",\"1.1.1.1\""
#define MQTT_DEVICE_ID  "TIH_NODE_01"
#define MQTT_TOKEN      "Canopy/data"
#define MQTT_USER       "user2"
#define MQTT_PASS       "12345"
#define TIME_OUT        6000U

/* Raw MQTT topics, same as device_simulator.py / onem2m_bridge.py */
#define MQTT_TOPIC_DATA         "data"
#define MQTT_TOPIC_MOTOR_CMD    "motor_cmd"
#define MQTT_TOPIC_MOTOR_STATUS "motor_status"
#define MQTT_TOPIC_OTA          "ota"
#define MQTT_TOPIC_CMD          "cmd"
#define MQTT_TTI_UNIT_SECONDS   60U     // Motor run time per TTI unit

/* GSM_MQTT_eventPending() flags: unsolicited URCs seen since the last command */
#define MQTT_EVT_RECV   0x01    // "+QMTRECV:" - a message is waiting in the modem
#define MQTT_EVT_STAT   0x02    // "+QMTSTAT:" - the modem reports the link dropped

void GSM_init(UART_HandleTypeDef *huart);

void GSM_UART_TxCpltCallback(void);
void GSM_UART_RxCpltCallback(void);
void GSM_txData(char *data, uint16_t length);
void GSM_rxData(char *data, uint16_t length);
void GSM_resetRxBuff(void);
void GSM_power_up(void);
void GSM_power_down(void);
void GSM_READY(void);
#if (USE_HTTP_UPLOAD == 1)
uint8_t GSM_HTTP_sendDataToServer(char *payload);
#endif
uint8_t GSM_time_sync(char *url);
uint8_t GSM_httpGet(char *url, char *resp);
uint8_t GSM_MQTT_sendDataToServer(char *data);
uint8_t GSM_MQTT_SSL_sendDataToServer(char *data);
uint8_t GSM_isAlive(void);
uint8_t GSM_MQTT_connect(const char *clientId);
uint8_t GSM_MQTT_isConnected(void);
uint8_t GSM_MQTT_publish(const char *topic, const char *payload, uint8_t qos, uint8_t retain);
uint8_t GSM_MQTT_readMessage(char *topic, uint16_t topicSize, char *payload, uint16_t payloadSize);
uint8_t GSM_MQTT_eventPending(void);
void GSM_MQTT_logLinkClosed(void);
uint8_t GSM_getrxState(void);
uint8_t GSM_networkCheck(void);
uint8_t responseTimeOut(void);
void updateLastRespTime(void);
uint8_t _checkOKinResp(char *);
void _waitForResp(uint16_t, uint16_t);


#ifdef __cplusplus
}
#endif

#endif /* GSM_EC200U_H_ */
