#ifndef GSM_EC200U_H_
#define GSM_EC200U_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SIM_APN         "airtelgprs.com"
#define TIME_SYNC_URL   "http://www.tih.iitb.ac.in/"
// #define SERVER_URL      "https://eagris.tihiitb.org/eagris/v1/sensor-data/public/receive/data"
#define MQTT_HOST       "test.mosquitto.org"//"mqtt.tihiitb.org"
#define MQTT_SSL_HOST   "mqtt.tihiitb.org"
#define MQTT_PORT       "1883"
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
// uint8_t GSM_HTTP_sendDataToServer(char *payload); // HTTP upload replaced by MQTT
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
