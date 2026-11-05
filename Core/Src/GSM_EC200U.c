#include "GSM_EC200U.h"

/* Holds a full command response. Must fit the modem echoing an ~800-byte MQTT
   publish payload plus the +QMTPUBEX result, and an incoming OTA message. Was [100]. */
char aRxBuffer[1024];
uint16_t RX_buff_index = 0;
uint8_t rxStateFlag = RESET;
UART_HandleTypeDef *huart;
long lastRespTime = 0;
char opratorAPN[25];
extern uint8_t RSSI;
extern uint32_t uid_stm32;
extern char print_char[200];

TIME_TypeDef timeObj;

#define NET_REG_TIMEOUT_MS      90000U  // first registration after power-up can take ~50 s
#define QLTS_TIMEOUT_MS         60000U  // network time can arrive well after registration
#define QIACT_TIMEOUT_MS        60000U
#define QIACT_ATTEMPTS          3U      // activation often fails right after registration
#define QIACT_RETRY_DELAY_MS    5000U

static char *_waitForLine(const char *token, uint32_t timeout);
static char *_sendCommand(const char *cmd, const char *token, uint32_t timeout);
static uint8_t _waitForRegistration(uint32_t timeout);

void GSM_init(UART_HandleTypeDef *_huart)
{
	huart = _huart;
	GSM_resetRxBuff();
}

void GSM_UART_TxCpltCallback(void)
{
}

void GSM_UART_RxCpltCallback(void)
{
	if (RX_buff_index >= sizeof(aRxBuffer) - 1)
	{
		return;
	}

	RX_buff_index++;
	if (huart != NULL && huart->Lock == HAL_LOCKED)
	{
		__HAL_UNLOCK(huart);
	}
	if (huart != NULL)
	{
		__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF);
		uint8_t ret = HAL_UART_Receive_IT(huart, (uint8_t *)aRxBuffer + RX_buff_index, 1);
		if (ret != HAL_OK)
		{
			printDisplay_level(35, "grc");
			Error_Handler();
		}
	}
	rxStateFlag = RESET;
	updateLastRespTime();
}

void GSM_txData(char *data, uint16_t length)
{
	if (huart == NULL || data == NULL)
	{
		return;
	}

	while (huart->gState != HAL_UART_STATE_READY)
	{
	}
	__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF);
	if (HAL_UART_Transmit_IT(huart, (uint8_t *)data, length) != HAL_OK)
	{
		printDisplay_level(35, "gtd");
		Error_Handler();
	}
	serial_print((char *)data);
}

void GSM_rxData(char *data, uint16_t length)
{
	if (huart == NULL || data == NULL)
	{
		return;
	}
	if (huart->RxState != HAL_UART_STATE_READY)
	{
		HAL_UART_AbortReceive(huart);
	}
	if (length > sizeof(aRxBuffer))
	{
		length = sizeof(aRxBuffer);
	}
	__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF);
	if (HAL_UART_Receive_IT(huart, (uint8_t *)data, length) != HAL_OK)
	{
		printDisplay_level(35, "grd");
		Error_Handler();
	}
	rxStateFlag = SET;
	updateLastRespTime();
}

void GSM_power_up()
{
	serial_print("GSM Power Up\r\n");
	HAL_GPIO_WritePin(GSM_KEY_GPIO_Port, GSM_KEY_Pin, GPIO_PIN_SET);
	HAL_Delay(2050);
	HAL_GPIO_WritePin(GSM_KEY_GPIO_Port, GSM_KEY_Pin, GPIO_PIN_RESET);
	char str[15];
	sprintf(str, "ID:%08lX", uid_stm32);
	printDisplay(str);
	HAL_Delay(20000);

	char *AtCommand = "AT+GSN\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	snprintf(print_char, sizeof(print_char), "\r\nGSM IMEI %s \r\n", aRxBuffer);
	serial_print(print_char);
}
void GSM_power_down()
{
	char *AtCommand = "AT+QPOWD=1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	serial_print(aRxBuffer);
}

void GSM_READY(void)
{
	GSM_resetRxBuff();
	while (RX_buff_index < 3 || memcmp(aRxBuffer + 2, "RDY", 3) != 0)
	{
		HAL_Delay(1000);
	}
	serial_print("Received: RDY");
}

void GSM_resetRxBuff(void)
{
	RX_buff_index = 0;
	if (huart != NULL && huart->RxState != HAL_UART_STATE_READY)
	{
		HAL_UART_AbortReceive(huart);
	}
	memset(aRxBuffer, 0, sizeof(aRxBuffer));
	if (huart != NULL)
	{
		__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF);
		HAL_StatusTypeDef ret = HAL_UART_Receive_IT(huart, (uint8_t *)aRxBuffer, 1);
		if (ret != HAL_OK)
		{
			printDisplay_level(35, "grb");
			Error_Handler();
		}
	}
	updateLastRespTime();
}

void assignTime(TIME_TypeDef *obj, char *resp)
{
	uint8_t x, y;
	const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
	const char *month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	// assign hours
	x = resp[82] - 48;
	y = resp[83] - 48;
	obj->hours = (x * 10) + y;

	// assign minutes
	x = resp[85] - 48;
	y = resp[86] - 48;
	obj->minutes = (x * 10) + y;

	// assign seconds
	x = resp[88] - 48;
	y = resp[89] - 48;
	obj->seconds = (x * 10) + y;

	// assign weekday
	obj->weekday = 0;
	char day_name[4];
	day_name[0] = resp[65];
	day_name[1] = resp[66];
	day_name[2] = resp[67];
	day_name[3] = 0;

	for (int i = 1; i < 8; i++)
	{
		if (strcmp(day_name, day_names[i - 1]) == 0)
		{
			obj->weekday = i;
			break;
		}
	}

	// assign month
	obj->month = 0;
	char month_name[4];
	month_name[0] = resp[73];
	month_name[1] = resp[74];
	month_name[2] = resp[75];
	month_name[3] = 0;

	for (int i = 1; i < 12; i++)
	{
		if (strcmp(month_name, month_names[i - 1]) == 0)
		{
			obj->month = i;
			break;
		}
	}

	// assign date
	x = resp[70] - 48;
	y = resp[71] - 48;
	obj->date = (x * 10) + y;

	// assign year
	x = resp[79] - 48;
	y = resp[80] - 48;
	obj->year = (x * 10) + y;
}
uint8_t GSM_time_sync(char *url)
{
	if (url == NULL)
	{
		return RESET;
	}

	char dataBuffForTime[120] = {0};
	uint8_t ret = RESET;
	uint8_t retry = 0;

	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	/* The modem only knows the time once it has registered and received the
	   network's time broadcast, which can take a while after power-up, so
	   wait for registration and keep asking before falling back to HTTP. */
	if (_waitForRegistration(NET_REG_TIMEOUT_MS))
	{
		uint32_t startTime = HAL_GetTick();
		do
		{
			/* +QLTS: "yyyy/MM/dd,hh:mm:ss+zz,dst" - already GMT; zz is only
			   the zone offset and must not be applied */
			char *p = _sendCommand("AT+QLTS=1\r", "+QLTS:", 3000);
			int yr, mo, dd, hh, mi, ss;

			if (p != NULL && sscanf(p, "+QLTS: \"%d/%d/%d,%d:%d:%d", &yr, &mo, &dd, &hh, &mi, &ss) == 6 && yr >= 2024)
			{
				timeObj.year = (uint8_t)(yr % 100);
				timeObj.month = (uint8_t)mo;
				timeObj.date = (uint8_t)dd;
				timeObj.hours = (uint8_t)hh;
				timeObj.minutes = (uint8_t)mi;
				timeObj.seconds = (uint8_t)ss;
				rtc_set_time(timeObj);
				return SET;
			}
			HAL_Delay(5000);
		} while (HAL_GetTick() - startTime < QLTS_TIMEOUT_MS);
		serial_print("Network time not received, trying HTTP");
	}

	ret = GSM_httpGet(url, dataBuffForTime);
	if (ret == SET)
	{
		assignTime(&timeObj, dataBuffForTime);
		HAL_Delay(1000);
		rtc_set_time(timeObj);
	}
	return ret;
}

#if (USE_HTTP_UPLOAD == 1)
/* HTTP POST upload to SERVER_URL - the alternative to the MQTT "data"
   publish, selected by USE_HTTP_UPLOAD in GSM_EC200U.h */
uint8_t GSM_HTTP_sendDataToServer(char *data)
{
	if (data == NULL)
	{
		return RESET;
	}

	uint8_t retry = 0;
	char *AtCommand;
	uint8_t ret = SET;
	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	AtCommand = "AT+QHTTPCFG=\"contextid\",1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QHTTPCFG=\"responseheader\",1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QHTTPCFG=\"contenttype\",1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	char tempBuff[60];
	memset(tempBuff, 0, sizeof(tempBuff));
	sprintf(tempBuff, "AT+QICSGP=1,1,\"%s\",\"\",\"\",1\r", opratorAPN);
	GSM_txData(tempBuff, strlen(tempBuff));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QIACT=1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QIACT?\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	memset(tempBuff, 0, sizeof(tempBuff));
	snprintf(tempBuff, sizeof(tempBuff), "AT+QHTTPURL=%d,80\r", strlen(SERVER_URL));
	/* After CONNECT the modem takes the next <len> bytes as the URL, so only
	   send it once CONNECT is seen, and never bail out after it */
	if (_sendCommand(tempBuff, "CONNECT", 5000) == NULL)
	{
		serial_print("HTTP: no CONNECT for the URL");
		return RESET;
	}
	GSM_txData(SERVER_URL, strlen(SERVER_URL));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (_checkOKinResp(aRxBuffer + RX_buff_index - 4) != SET)
	{
		return RESET;
	}

	char tempPtr[300];
	memset(tempPtr, 0, sizeof(tempPtr));
	snprintf(tempPtr, sizeof(tempPtr), "AT+QHTTPPOST=%d,10,12\r", strlen(data));
	GSM_txData(tempPtr, strlen(tempPtr));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	HAL_Delay(1000);

	GSM_txData(data, strlen(data));
	GSM_resetRxBuff();
	/* Result: "+QHTTPPOST: <err>[,<status>[,<content_length>]]". Read the
	   numbers rather than fixed offsets: the trailing content length is only
	   present when the server sends Content-Length. Success = err 0, any 2xx. */
	char *result = _waitForLine("+QHTTPPOST:", 15000);
	snprintf(print_char, sizeof(print_char), "Received = %s", aRxBuffer);
	serial_print(print_char);
	long postErr = -1, httpStatus = 0;
	if (result != NULL)
	{
		char *next;
		postErr = strtol(result + strlen("+QHTTPPOST:"), &next, 10);
		if (*next == ',')
			httpStatus = strtol(next + 1, NULL, 10);
	}
	snprintf(print_char, sizeof(print_char), "HTTP POST err %ld, status %ld", postErr, httpStatus);
	serial_print(print_char);
	if (postErr != 0 || httpStatus < 200 || httpStatus > 299)
	{
		ret = RESET;
	}
	AtCommand = "AT+QHTTPSTOP\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	AtCommand = "AT+QIDEACT=1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	snprintf(print_char, sizeof(print_char), "Received = %s", aRxBuffer);
	serial_print(print_char);
	return ret;
}
#endif /* USE_HTTP_UPLOAD == 1 */

uint8_t GSM_httpGet(char *url, char *resp)
{
	if (url == NULL || resp == NULL)
	{
		return RESET;
	}
	uint8_t retry = 0;
	char *AtCommand;

	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	AtCommand = "AT+QHTTPCFG=\"contextid\",1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QHTTPCFG=\"responseheader\",1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	char tempBuff[60];
	memset(tempBuff, 0, sizeof(tempBuff));
	sprintf(tempBuff, "AT+QICSGP=1,1,\"%s\",\"\",\"\",1\r", opratorAPN);
	GSM_txData(tempBuff, strlen(tempBuff));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QIACT=1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QIACT?\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	memset(tempBuff, 0, sizeof(tempBuff));
	snprintf(tempBuff, sizeof(tempBuff), "AT+QHTTPURL=%d,80\r", strlen(url));
	/* The reply is "\r\nCONNECT\r\n", so search for it rather than expect it
	   at the end of the buffer. After CONNECT the modem takes the next <len>
	   bytes as the URL: bailing out there left it swallowing the following
	   AT commands for up to 80 s. */
	if (_sendCommand(tempBuff, "CONNECT", 5000) == NULL)
	{
		serial_print("HTTP: no CONNECT for the URL");
		return RESET;
	}
	GSM_txData(url, strlen(url));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (_checkOKinResp(aRxBuffer + RX_buff_index - 4) != SET)
	{
		return RESET;
	}

	AtCommand = "AT+QHTTPGET=80\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	_waitForResp(10000, 25);
	while (responseTimeOut())
		;
	serial_print(aRxBuffer);
	for (size_t i = 0; i + 2 < RX_buff_index; i++)
	{
		if (aRxBuffer[i] == '2' && aRxBuffer[i + 1] == '0' && aRxBuffer[i + 2] == '0')
		{
			AtCommand = "AT+QHTTPREAD=80\r";
			GSM_txData(AtCommand, strlen(AtCommand));
			GSM_rxData(resp, 100);
			while (responseTimeOut())
				;
			return SET;
		}
	}

	return RESET;
}

uint8_t GSM_MQTT_sendDataToServer(char *data)
{
	if (data == NULL)
	{
		return RESET;
	}
	char *AtCommand;

	AtCommand = "AT\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	HAL_Delay(1000);

	char tempBuff[sizeof(MQTT_HOST) + sizeof(MQTT_PORT) + 16];
	memset(tempBuff, 0, sizeof(MQTT_HOST) + sizeof(MQTT_PORT) + 16);
	sprintf(tempBuff, "AT+QMTOPEN=0,\"%s\",%s\r", MQTT_HOST, MQTT_PORT);
	GSM_txData(tempBuff, strlen(tempBuff));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QMTCONN=0,\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_DEVICE_ID, strlen(MQTT_DEVICE_ID));
	AtCommand = "\",\"\",\"\"\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QMTSUB=0,1,\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_TOKEN, strlen(MQTT_TOKEN));
	AtCommand = "\",1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QMTPUBEX=0,0,0,0,\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_TOKEN, strlen(MQTT_TOKEN));
	memset(tempBuff, 0, sizeof(MQTT_HOST) + sizeof(MQTT_PORT) + 16);
	sprintf(tempBuff, "\",%d\r", strlen(data));
	GSM_txData(tempBuff, strlen(tempBuff));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	GSM_txData(data, strlen(data));
	HAL_Delay(1000);

	return SET;
}

uint8_t GSM_MQTT_SSL_sendDataToServer(char *data)
{
	if (data == NULL)
	{
		return RESET;
	}
	uint8_t retry = 0, respLength;
	char *AtCommand;

	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	AtCommand = "AT+QMTCFG=\"SSL\",0,1,2\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"cacert\",2,\"UFS:cacert.pem\"\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"clientcert\",2,\"UFS:client.pem\"\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"clientkey\",2,\"UFS:user_key.pem\"\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"seclevel\",2,2\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"sslversion\",2,4\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"ciphersuite\",2,0xFFFF\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QSSLCFG=\"ignorelocaltime\",2,1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QMTOPEN=0,\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_SSL_HOST, strlen(MQTT_SSL_HOST));
	AtCommand = "\",";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_PORT, strlen(MQTT_PORT));
	AtCommand = "\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	respLength = 40 + strlen(MQTT_SSL_HOST) + strlen(MQTT_PORT);
	_waitForResp(5000, respLength);
	serial_print(aRxBuffer);
	if (aRxBuffer[respLength - 3] != 48 || aRxBuffer[respLength - 5] != 48)
	{
		AtCommand = "AT+QMTCLOSE=0\r";
		GSM_txData(AtCommand, strlen(AtCommand));
		GSM_resetRxBuff();
		while (responseTimeOut())
			;

		return RESET;
	}

	AtCommand = "AT+QMTCONN=0,\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_DEVICE_ID, strlen(MQTT_DEVICE_ID));
	AtCommand = "\",\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_USER, strlen(MQTT_USER));
	AtCommand = "\",\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_PASS, strlen(MQTT_PASS));
	AtCommand = "\"\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	respLength = 47 + strlen(MQTT_DEVICE_ID) + strlen(MQTT_USER) + strlen(MQTT_PASS);
	_waitForResp(2000, respLength);
	serial_print(aRxBuffer);
	if (aRxBuffer[respLength - 3] != 48 || aRxBuffer[respLength - 5] != 48 ||
		aRxBuffer[respLength - 7] != 48)
	{
		AtCommand = "AT+QMTDISC=0\r";
		GSM_txData(AtCommand, strlen(AtCommand));
		GSM_resetRxBuff();
		while (responseTimeOut())
			;

		AtCommand = "AT+QMTCLOSE=0\r";
		GSM_txData(AtCommand, strlen(AtCommand));
		GSM_resetRxBuff();
		while (responseTimeOut())
			;

		return RESET;
	}

	AtCommand = "AT+QMTPUBEX=0,0,0,0,\"";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_txData(MQTT_TOKEN, strlen(MQTT_TOKEN));
	char dataLenInStr[10];
	memset(dataLenInStr, 0, sizeof(dataLenInStr));
	sprintf(dataLenInStr, "\",%d\r", strlen(data));
	GSM_txData(dataLenInStr, strlen(dataLenInStr));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	uint8_t retVal = RESET;
	if (aRxBuffer[RX_buff_index - 2] == 62)
	{
		GSM_txData(data, strlen(data));
		retVal = SET;
	}
	serial_print(aRxBuffer);

	AtCommand = "AT+QMTDISC=0\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	AtCommand = "AT+QMTCLOSE=0\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	return retVal;
}

/* ---------------------------------------------------------------------------
 * Persistent MQTT client (used by MQTT_app_* in main.c)
 *
 * Unlike the functions above, these clear the RX buffer *before* sending, so
 * the reply (and the command echo) is never lost to the reset, and they parse
 * replies by searching for tokens rather than fixed offsets.
 * Incoming messages use the modem's buffer mode (recv/mode 1): the modem only
 * raises "+QMTRECV: 0,<id>" and keeps the message until GSM_MQTT_readMessage()
 * fetches it, so nothing is lost while the MCU is busy with a sensor cycle.
 * ------------------------------------------------------------------------- */

/* Wait for `token` followed by the end of its line, searching from
   aRxBuffer + from. NULL on timeout or ERROR. */
static char *_waitForLineFrom(const char *token, uint16_t from, uint32_t timeout)
{
	uint32_t startTime = HAL_GetTick();
	while (HAL_GetTick() - startTime < timeout)
	{
		char *p = strstr(aRxBuffer + from, token);
		if (p != NULL && strstr(p, "\r\n") != NULL)
			return p;
		if (strstr(aRxBuffer + from, "ERROR") != NULL)
			return NULL;
	}
	return NULL;
}

static char *_waitForLine(const char *token, uint32_t timeout)
{
	return _waitForLineFrom(token, 0, timeout);
}

/* The idx-th comma-separated integer of a result line (0 = the first one),
   or -1 if the line has no such field */
static long _resultField(const char *s, uint8_t idx)
{
	for (; idx > 0; idx--)
	{
		while (*s != ',' && *s != '\r' && *s != '\0')
			s++;
		if (*s != ',')
			return -1;
		s++;
	}
	return strtol(s, NULL, 10);
}

/* p points at a "<token><result>,..." line from the modem's MQTT client, where
   result 0 = done, 1 = retransmitting, 2 = failed. While it says 1, wait for
   the next such line instead of treating it as a failure. Returns the line
   with the final result, or NULL on timeout. */
static char *_waitPastRetransmit(char *p, const char *token, uint32_t timeout)
{
	uint32_t startTime = HAL_GetTick();

	while (p != NULL && _resultField(p + strlen(token), 0) == 1)
	{
		uint32_t elapsed = HAL_GetTick() - startTime;
		if (elapsed >= timeout)
			return NULL;
		p = _waitForLineFrom(token, (uint16_t)(p - aRxBuffer) + 1U, timeout - elapsed);
	}
	return p;
}

static char *_sendCommand(const char *cmd, const char *token, uint32_t timeout)
{
	GSM_resetRxBuff();
	GSM_txData((char *)cmd, strlen(cmd));
	return _waitForLine(token, timeout);
}

static void _mqttClose(void)
{
	_sendCommand("AT+QMTDISC=0\r", "+QMTDISC: 0,", 5000);
	_sendCommand("AT+QMTCLOSE=0\r", "+QMTCLOSE: 0,", 5000);
}

/* Modem answers AT - used at boot so an already-running modem (e.g. after the
   scheduled MCU restart) isn't switched *off* by the PWRKEY pulse. */
uint8_t GSM_isAlive(void)
{
	return _sendCommand("AT\r", "OK\r\n", 2000) != NULL;
}

/* Ask the modem why the last step failed, and log its replies */
static void _logDiagnostics(void)
{
	static const char *const cmds[] = {
		"AT+CPIN?\r",	   // SIM: READY, SIM PIN, or an error if not inserted
		"AT+CFUN?\r",	   // radio: 1 = full functionality, 0/4 = radio off
		"AT+QIGETERROR\r", // last TCP/IP error (activation, DNS, ...)
		"AT+CEER\r",	   // network's reason for the last reject
		"AT+CSQ\r",		   // signal: rssi 0-31, 99 = none
		"AT+COPS?\r",	   // operator
		"AT+QNWINFO\r",	   // access technology and band
		"AT+CGATT?\r",	   // packet-domain attach
	};

	serial_print("---- GSM diagnostics ----");
	for (uint8_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
	{
		_sendCommand(cmds[i], "OK\r\n", 5000);
		serial_print(aRxBuffer);
	}
}

/* <stat> from the AT+CEREG? (LTE) / AT+CREG? reply "+C(E)REG: <n>,<stat>",
   or -1 if there was no reply */
static long _registrationStatus(const char *cmd, const char *token)
{
	char *p = _sendCommand(cmd, token, 2000);

	return (p != NULL) ? _resultField(p + strlen(token), 1) : -1;
}

/* Registered on the network, not merely answering AT+CREG? with OK */
static uint8_t _waitForRegistration(uint32_t timeout)
{
	uint32_t startTime = HAL_GetTick();
	long lteLogged = -2, gsmLogged = -2;

	do
	{
		long lte = _registrationStatus("AT+CEREG?\r", "+CEREG:");
		long gsm = _registrationStatus("AT+CREG?\r", "+CREG:");

		if (lte == 1 || lte == 5 || gsm == 1 || gsm == 5)
			return SET;
		if (lte != lteLogged || gsm != gsmLogged)
		{
			/* Log only changes, so a long wait doesn't flood the log */
			snprintf(print_char, sizeof(print_char),
					 "GSM: registration LTE %ld, GSM %ld (1 home, 5 roaming, 2 searching, 3 denied, 0 not searching, -1 no reply)",
					 lte, gsm);
			serial_print(print_char);
			lteLogged = lte;
			gsmLogged = gsm;
		}
		HAL_Delay(2000);
	} while (HAL_GetTick() - startTime < timeout);

	serial_print("GSM: not registered on the network");
	_logDiagnostics();
	return RESET;
}

/* Data connection on PDP context 1, then public DNS on it. AT+QICSGP is
   rejected while the context is active, so only configure and activate it
   when it isn't up already. */
static uint8_t _dataConnectionUp(void)
{
	char cmd[80];
	uint8_t active = RESET;

	if (_sendCommand("AT+QIACT?\r", "OK\r\n", 3000) != NULL && strstr(aRxBuffer, "+QIACT: 1,1") != NULL)
	{
		active = SET;
	}
	else
	{
		snprintf(cmd, sizeof(cmd), "AT+QICSGP=1,1,\"%s\",\"\",\"\",1\r", opratorAPN);
		if (_sendCommand(cmd, "OK\r\n", 5000) == NULL)
		{
			serial_print("GSM: could not set the APN");
			return RESET;
		}
		for (uint8_t i = 0; i < QIACT_ATTEMPTS && !active; i++)
		{
			if (i > 0U)
				HAL_Delay(QIACT_RETRY_DELAY_MS);
			active = (_sendCommand("AT+QIACT=1\r", "OK\r\n", QIACT_TIMEOUT_MS) != NULL);
		}
		if (!active)
		{
			serial_print("GSM: could not activate the data connection");
			_logDiagnostics();
			return RESET;
		}
		_sendCommand("AT+QIACT?\r", "OK\r\n", 3000);
	}
	serial_print(aRxBuffer); // +QIACT: 1,1,1,"<assigned IP>"

	snprintf(cmd, sizeof(cmd), "AT+QIDNSCFG=1,%s\r", GSM_DNS_SERVERS);
	if (_sendCommand(cmd, "OK\r\n", 5000) == NULL)
		serial_print("GSM: could not set the DNS servers");
	return SET;
}

static const char *_qmtopenReason(long result)
{
	switch (result)
	{
	case -1:
		return "failed to open network";
	case 1:
		return "wrong parameter";
	case 2:
		return "MQTT identifier occupied";
	case 3:
		return "failed to activate PDP context";
	case 4:
		return "failed to resolve broker name (DNS)";
	case 5:
		return "network connection error";
	default:
		return "no result (timeout or ERROR)";
	}
}

static const char *_connackReason(long retCode)
{
	switch (retCode)
	{
	case 1:
		return "unacceptable protocol version";
	case 2:
		return "client ID rejected";
	case 3:
		return "server unavailable";
	case 4:
		return "bad username or password";
	case 5:
		return "not authorized";
	default:
		return "no CONNACK";
	}
}

uint8_t GSM_MQTT_connect(const char *clientId)
{
	char cmd[120];
	char *p;
	long result;
	uint8_t retry = 0;

	if (clientId == NULL)
	{
		return RESET;
	}

	/* Also sets the APN up on its first success */
	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	if (!_waitForRegistration(NET_REG_TIMEOUT_MS) || !_dataConnectionUp())
	{
		return RESET;
	}

	/* Drop any session left open from before an MCU reset */
	_mqttClose();

	/* MQTT 3.1.1, and a keepalive short enough to keep the mobile network
	   from dropping the idle connection */
	_sendCommand("AT+QMTCFG=\"version\",0,4\r", "OK\r\n", 5000);
	snprintf(cmd, sizeof(cmd), "AT+QMTCFG=\"keepalive\",0,%u\r", MQTT_KEEPALIVE_S);
	_sendCommand(cmd, "OK\r\n", 5000);
	if (_sendCommand("AT+QMTCFG=\"recv/mode\",0,1,0\r", "OK\r\n", 5000) == NULL)
	{
		serial_print("MQTT: recv/mode rejected");
		return RESET;
	}

	/* +QMTOPEN: 0,<result> */
	snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%s\r", MQTT_HOST, MQTT_PORT);
	p = _sendCommand(cmd, "+QMTOPEN: 0,", 75000);
	result = (p != NULL) ? _resultField(p + 12, 0) : -99;
	if (result == 2)
	{
		/* A previous network connection is still allocated: close it, open again */
		_sendCommand("AT+QMTCLOSE=0\r", "+QMTCLOSE: 0,", 5000);
		p = _sendCommand(cmd, "+QMTOPEN: 0,", 75000);
		result = (p != NULL) ? _resultField(p + 12, 0) : -99;
	}
	if (result != 0)
	{
		snprintf(print_char, sizeof(print_char), "MQTT: open %s:%s failed: %s", MQTT_HOST, MQTT_PORT, _qmtopenReason(result));
		serial_print(print_char);
		_logDiagnostics();
		_mqttClose();
		return RESET;
	}

	/* +QMTCONN: 0,<result>[,<ret_code>] */
	snprintf(cmd, sizeof(cmd), "AT+QMTCONN=0,\"%s\"\r", clientId);
	p = _waitPastRetransmit(_sendCommand(cmd, "+QMTCONN: 0,", 15000), "+QMTCONN: 0,", 30000);
	if (p == NULL || _resultField(p + 12, 0) != 0 || _resultField(p + 12, 1) != 0)
	{
		snprintf(print_char, sizeof(print_char), "MQTT: connect failed: %s",
				 _connackReason((p != NULL) ? _resultField(p + 12, 1) : -1));
		serial_print(print_char);
		_mqttClose();
		return RESET;
	}

	/* +QMTSUB: 0,1,<result>,... (message ID 1) */
	snprintf(cmd, sizeof(cmd), "AT+QMTSUB=0,1,\"%s\",1,\"%s\",1,\"%s\",1\r",
			 MQTT_TOPIC_MOTOR_CMD, MQTT_TOPIC_OTA, MQTT_TOPIC_CMD);
	p = _waitPastRetransmit(_sendCommand(cmd, "+QMTSUB: 0,1,", 15000), "+QMTSUB: 0,1,", 30000);
	if (p == NULL || _resultField(p + 13, 0) != 0)
	{
		serial_print("MQTT: subscribe failed");
		_mqttClose();
		return RESET;
	}

	snprintf(print_char, sizeof(print_char), "MQTT: connected to %s:%s and subscribed", MQTT_HOST, MQTT_PORT);
	serial_print(print_char);
	return SET;
}

/* Asks the modem for the MQTT link state. Logs why when it isn't connected:
   no reply only means the modem didn't answer in time, not that the link is down. */
uint8_t GSM_MQTT_isConnected(void)
{
	char *p;
	long state;

	if (_sendCommand("AT+QMTCONN?\r", "OK\r\n", 3000) == NULL)
	{
		serial_print("MQTT: no reply to AT+QMTCONN? (modem busy or not answering)");
		return RESET;
	}
	/* "+QMTCONN: 0,<state>": 1 initializing, 2 connecting, 3 connected,
	   4 disconnecting. No such line = the client isn't open at all. */
	p = strstr(aRxBuffer, "+QMTCONN: 0,");
	if (p == NULL)
	{
		serial_print("MQTT: client not open (the modem has no MQTT link)");
		return RESET;
	}
	state = _resultField(p + 12, 0);
	if (state != 3)
	{
		snprintf(print_char, sizeof(print_char),
				 "MQTT: link state %ld, not connected (1 initializing, 2 connecting, 4 disconnecting)", state);
		serial_print(print_char);
		return RESET;
	}
	return SET;
}

static const char *_qmtstatReason(long code)
{
	switch (code)
	{
	case 1:
		return "connection closed or reset by the broker/network (also how a broker drops a client whose ID connected elsewhere)";
	case 2:
		return "keepalive PINGREQ timed out or failed";
	case 3:
		return "sending CONNECT timed out or failed";
	case 4:
		return "no CONNACK from the broker";
	case 5:
		return "client sent DISCONNECT and the broker closed the connection";
	case 6:
		return "modem gave up after repeated send failures";
	case 7:
		return "link not alive or broker unavailable";
	default:
		return "unknown";
	}
}

/* Log the "+QMTSTAT: 0,<code>" URC (the modem reporting the MQTT link closed)
   waiting in aRxBuffer. Call before any other command clears the buffer. */
void GSM_MQTT_logLinkClosed(void)
{
	char *p = strstr(aRxBuffer, "+QMTSTAT:");
	char *end = (p != NULL) ? strstr(p, "\r\n") : NULL;
	long code = (p != NULL) ? _resultField(p + strlen("+QMTSTAT:"), 1) : -1;

	/* Raw line too, in case the format differs from "+QMTSTAT: 0,<code>" */
	snprintf(print_char, sizeof(print_char), "MQTT: modem reports link closed (%.*s): %s",
			 (end != NULL) ? (int)(end - p) : 0, (p != NULL) ? p : "", _qmtstatReason(code));
	serial_print(print_char);
}

uint8_t GSM_MQTT_publish(const char *topic, const char *payload, uint8_t qos, uint8_t retain)
{
	static uint16_t msgId = 0;
	uint16_t id = 0; // must be 0 for QoS 0
	char cmd[80];
	char token[24];
	char *p;
	uint32_t startTime;

	if (topic == NULL || payload == NULL)
	{
		return RESET;
	}
	if (qos > 0)
	{
		if (++msgId == 0)
			msgId = 1;
		id = msgId;
	}

	snprintf(cmd, sizeof(cmd), "AT+QMTPUBEX=0,%u,%u,%u,\"%s\",%u\r",
			 id, qos, retain, topic, (unsigned int)strlen(payload));
	GSM_resetRxBuff();
	GSM_txData(cmd, strlen(cmd));

	startTime = HAL_GetTick();
	while (strchr(aRxBuffer, '>') == NULL)
	{
		if (strstr(aRxBuffer, "ERROR") != NULL)
		{
			/* QMTPUBEX is rejected outright when the MQTT link is not connected */
			snprintf(print_char, sizeof(print_char), "MQTT: publish to '%s' rejected with ERROR (link not connected?)", topic);
			serial_print(print_char);
			return RESET;
		}
		if (HAL_GetTick() - startTime > 5000)
		{
			snprintf(print_char, sizeof(print_char), "MQTT: no publish prompt for '%s' within 5 s", topic);
			serial_print(print_char);
			return RESET;
		}
	}

	snprintf(token, sizeof(token), "+QMTPUBEX: 0,%u,", id);
	GSM_resetRxBuff();
	GSM_txData((char *)payload, strlen(payload));
	/* +QMTPUBEX: 0,<id>,<result>: 0 = sent (and acked for QoS 1),
	   1 = retransmitting (no PUBACK yet), 2 = failed */
	p = _waitPastRetransmit(_waitForLine(token, 20000), token, 30000);
	if (p == NULL || _resultField(p + strlen(token), 0) != 0)
	{
		snprintf(print_char, sizeof(print_char), "MQTT: publish to '%s' failed (result %ld)", topic,
				 (p != NULL) ? _resultField(p + strlen(token), 0) : -1L);
		serial_print(print_char);
		return RESET;
	}
	return SET;
}

/* Fetch one buffered incoming message. RESET if none is waiting. */
uint8_t GSM_MQTT_readMessage(char *topic, uint16_t topicSize, char *payload, uint16_t payloadSize)
{
	char cmd[24];
	char *p, *start, *end;
	int8_t recvId = -1;
	uint16_t len;

	if (topic == NULL || payload == NULL || topicSize == 0 || payloadSize == 0)
	{
		return RESET;
	}

	/* "+QMTRECV: 0,s0,s1,s2,s3,s4" - sN is 1 when buffer slot N holds a message */
	if (_sendCommand("AT+QMTRECV?\r", "OK\r\n", 3000) == NULL)
	{
		return RESET;
	}
	p = strstr(aRxBuffer, "+QMTRECV: 0,");
	if (p == NULL)
	{
		return RESET;
	}
	p += 12;
	for (int8_t i = 0; i < 5; i++)
	{
		if (*p == '1')
		{
			recvId = i;
			break;
		}
		p = strchr(p, ',');
		if (p == NULL)
			break;
		p++;
	}
	if (recvId < 0)
	{
		return RESET;
	}

	/* "+QMTRECV: 0,<msgid>,"<topic>","<payload>"" - payload may itself contain quotes */
	snprintf(cmd, sizeof(cmd), "AT+QMTRECV=0,%d\r", recvId);
	if (_sendCommand(cmd, "OK\r\n", 3000) == NULL)
	{
		return RESET;
	}
	p = strstr(aRxBuffer, "+QMTRECV: 0,");
	if (p == NULL || (start = strchr(p, '"')) == NULL || (end = strchr(++start, '"')) == NULL)
	{
		return RESET;
	}
	len = end - start;
	if (len >= topicSize)
		len = topicSize - 1;
	memcpy(topic, start, len);
	topic[len] = 0;

	if ((start = strchr(end + 1, '"')) == NULL || (end = strstr(++start, "\r\n")) == NULL)
	{
		return RESET;
	}
	while (end > start && *end != '"')
		end--;
	len = end - start;
	if (len >= payloadSize)
		len = payloadSize - 1;
	memcpy(payload, start, len);
	payload[len] = 0;

	return SET;
}

/* Check, without any AT traffic, for URCs that arrived since the last command */
uint8_t GSM_MQTT_eventPending(void)
{
	uint8_t events = 0;
	char *p;

	/* Only count a URC once its whole line is in: the main loop polls far
	   faster than the modem sends (~87 us per byte), so acting on the bare
	   token read "+QMTSTAT: 0,<code>" before the code had arrived */
	p = strstr(aRxBuffer, "+QMTRECV:");
	if (p != NULL && strstr(p, "\r\n") != NULL)
		events |= MQTT_EVT_RECV;
	p = strstr(aRxBuffer, "+QMTSTAT:");
	if (p != NULL && strstr(p, "\r\n") != NULL)
		events |= MQTT_EVT_STAT;
	return events;
}

uint8_t GSM_getrxState(void)
{
	return rxStateFlag;
}

uint8_t GSM_networkCheck(void)
{
	char *AtCommand;
	static uint8_t APNcheckFlag = SET;

	AtCommand = "AT\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (RX_buff_index < 4 || _checkOKinResp(aRxBuffer + RX_buff_index - 4) != SET)
	{
		return RESET;
	}

	AtCommand = "AT+CMGF=1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (_checkOKinResp(aRxBuffer + 12) != SET)
	{
		return RESET;
	}

	AtCommand = "AT+CSQ\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (aRxBuffer[17] == 44)
	{
		RSSI = (aRxBuffer[15] - 48) * 10 + (aRxBuffer[16] - 48);
	}
	else if (aRxBuffer[16] == 44)
	{
		RSSI = aRxBuffer[15] - 48;
	}
	else
	{
		RSSI = 0;
	}

	AtCommand = "AT+CREG?\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (_checkOKinResp(aRxBuffer + 25) != SET)
	{
		return RESET;
	}

	if (APNcheckFlag == RESET)
		return SET;

	AtCommand = "AT+CGDCONT=1,\"IP\",\"AIRTELIOT.COM\"\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	if (_checkOKinResp(aRxBuffer + 36) != SET)
	{
		return RESET;
	}
	AtCommand = "AT+CGDCONT?\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	serial_print(aRxBuffer);
	if (_checkOKinResp(aRxBuffer + RX_buff_index - 4) != SET)
	{
		return RESET;
	}
	uint16_t apnIndex, apnLen;
	for (uint16_t i = 22; i < RX_buff_index; i++)
	{
		static uint8_t numofcomma = 0;
		if (aRxBuffer[i] == 44)
		{
			numofcomma++;
			if (numofcomma == 2)
				apnIndex = i + 2;
			if (numofcomma > 2)
			{
				if (aRxBuffer[i - 1] == 34)
				{
					apnLen = i - 1 - apnIndex;
					break;
				}
				else
					return RESET;
			}
		}
	}
	APNcheckFlag = RESET;
	memset(opratorAPN, 0, sizeof(opratorAPN));
	memcpy(opratorAPN, aRxBuffer + apnIndex, apnLen);
	return SET;
}

uint8_t responseTimeOut(void)
{
	long currTime = HAL_GetTick();
	if (currTime - lastRespTime > TIME_OUT)
	{
		return RESET;
	}
	if ((currTime - lastRespTime > 250) && (RX_buff_index > 5))
		return RESET;
	else
		return SET;
}

inline void updateLastRespTime(void)
{
	lastRespTime = HAL_GetTick();
}

inline uint8_t _checkOKinResp(char *data)
{
	char *ok = "OK";
	return (memcmp(data, ok, 2) == 0);
}

inline void _waitForResp(uint16_t timeout, uint16_t len)
{
	long startTime = HAL_GetTick();
	while (HAL_GetTick() - startTime < timeout && RX_buff_index < len)
	{
	}
}
