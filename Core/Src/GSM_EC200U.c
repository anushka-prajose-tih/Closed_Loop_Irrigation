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
	char *AtCommand;

	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	AtCommand = "AT+QLTS=1\r";
	GSM_txData(AtCommand, strlen(AtCommand));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;

	while (GSM_getrxState())
		;

	for (size_t i = 0; i + 26 < RX_buff_index; i++)
	{
		if (memcmp(&aRxBuffer[i], "+QLTS:", 6) == 0)
		{
			char year[3] = {0};
			char month[3] = {0};
			char date[3] = {0};
			char hour[3] = {0};
			char min[3] = {0};
			char sec[3] = {0};

			year[0] = aRxBuffer[i + 10];
			year[1] = aRxBuffer[i + 11];
			timeObj.year = atoi(year);
			if (timeObj.year < 24)
			{
				break;
			}

			month[0] = aRxBuffer[i + 13];
			month[1] = aRxBuffer[i + 14];
			timeObj.month = atoi(month);

			date[0] = aRxBuffer[i + 16];
			date[1] = aRxBuffer[i + 17];
			timeObj.date = atoi(date);

			hour[0] = aRxBuffer[i + 19];
			hour[1] = aRxBuffer[i + 20];
			timeObj.hours = atoi(hour);

			min[0] = aRxBuffer[i + 22];
			min[1] = aRxBuffer[i + 23];
			timeObj.minutes = atoi(min);

			sec[0] = aRxBuffer[i + 25];
			sec[1] = aRxBuffer[i + 26];
			timeObj.seconds = atoi(sec);

			rtc_set_time(timeObj);

			return SET;
		}
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
static char *_waitForLine(const char *token, uint32_t timeout);

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
	GSM_txData(tempBuff, strlen(tempBuff));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	printf("Received = %s", aRxBuffer);
	char *tempResp = "CONNECT";
	if (RX_buff_index >= 9 && !memcmp(aRxBuffer + (RX_buff_index - 9), tempResp, strlen(tempResp)))
	{
		GSM_txData(SERVER_URL, strlen(SERVER_URL));
		GSM_resetRxBuff();
		while (responseTimeOut())
			;
		if (_checkOKinResp(aRxBuffer + RX_buff_index - 4) != SET)
		{
			return RESET;
		}
	}
	else
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
	sprintf(tempBuff, "AT+QICSGP=1,1,\"%s\","
					  ","
					  ",1\r",
			opratorAPN);
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
	GSM_txData(tempBuff, strlen(tempBuff));
	GSM_resetRxBuff();
	while (responseTimeOut())
		;
	char *tempResp = "CONNECT";
	if (RX_buff_index >= strlen(tempResp) && !memcmp(aRxBuffer + (RX_buff_index - strlen(tempResp)), tempResp, strlen(tempResp)))
	{
		GSM_txData(url, strlen(url));
		GSM_resetRxBuff();
		while (responseTimeOut())
			;
		if (_checkOKinResp(aRxBuffer + RX_buff_index - 4) != SET)
		{
			return RESET;
		}
	}
	else
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

/* Wait for `token` followed by the end of its line. NULL on timeout or ERROR. */
static char *_waitForLine(const char *token, uint32_t timeout)
{
	uint32_t startTime = HAL_GetTick();
	while (HAL_GetTick() - startTime < timeout)
	{
		char *p = strstr(aRxBuffer, token);
		if (p != NULL && strstr(p, "\r\n") != NULL)
			return p;
		if (strstr(aRxBuffer, "ERROR") != NULL)
			return NULL;
	}
	return NULL;
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

uint8_t GSM_MQTT_connect(const char *clientId)
{
	char cmd[120];
	char *p;
	uint8_t retry = 0;

	if (clientId == NULL)
	{
		return RESET;
	}

	while (!GSM_networkCheck())
	{
		retry++;
		if (retry > 2)
			return RESET;
	}

	/* PDP context, as in the HTTP path. QIACT answers ERROR if it is already
	   active, which is fine. */
	snprintf(cmd, sizeof(cmd), "AT+QICSGP=1,1,\"%s\",\"\",\"\",1\r", opratorAPN);
	_sendCommand(cmd, "OK\r\n", 5000);
	_sendCommand("AT+QIACT=1\r", "OK\r\n", 30000);

	/* Drop any session left open from before an MCU reset */
	_mqttClose();

	if (_sendCommand("AT+QMTCFG=\"recv/mode\",0,1,0\r", "OK\r\n", 5000) == NULL)
	{
		serial_print("MQTT: recv/mode rejected");
		return RESET;
	}

	snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%s\r", MQTT_HOST, MQTT_PORT);
	p = _sendCommand(cmd, "+QMTOPEN: 0,", 75000);
	if (p == NULL || (p[12] != '0' && p[12] != '2')) // 2 = already open
	{
		serial_print("MQTT: open failed");
		_mqttClose();
		return RESET;
	}

	snprintf(cmd, sizeof(cmd), "AT+QMTCONN=0,\"%s\"\r", clientId);
	p = _sendCommand(cmd, "+QMTCONN: 0,", 15000);
	if (p == NULL || strncmp(p + 12, "0,0", 3) != 0)
	{
		serial_print("MQTT: connect failed");
		_mqttClose();
		return RESET;
	}

	snprintf(cmd, sizeof(cmd), "AT+QMTSUB=0,1,\"%s\",1,\"%s\",1,\"%s\",1\r",
			 MQTT_TOPIC_MOTOR_CMD, MQTT_TOPIC_OTA, MQTT_TOPIC_CMD);
	p = _sendCommand(cmd, "+QMTSUB: 0,1,", 15000);
	if (p == NULL || p[13] != '0')
	{
		serial_print("MQTT: subscribe failed");
		_mqttClose();
		return RESET;
	}

	serial_print("MQTT: connected and subscribed");
	return SET;
}

uint8_t GSM_MQTT_isConnected(void)
{
	char *p;

	if (_sendCommand("AT+QMTCONN?\r", "OK\r\n", 3000) == NULL)
	{
		return RESET;
	}
	p = strstr(aRxBuffer, "+QMTCONN: 0,");
	return (p != NULL && p[12] == '3'); // 3 = connected
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
		if (HAL_GetTick() - startTime > 5000 || strstr(aRxBuffer, "ERROR") != NULL)
		{
			serial_print("MQTT: no publish prompt");
			return RESET;
		}
	}

	snprintf(token, sizeof(token), "+QMTPUBEX: 0,%u,", id);
	GSM_resetRxBuff();
	GSM_txData((char *)payload, strlen(payload));
	p = _waitForLine(token, 20000);
	if (p == NULL || p[strlen(token)] != '0') // 0 = sent (and acked for QoS 1)
	{
		serial_print("MQTT: publish failed");
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

	if (strstr(aRxBuffer, "+QMTRECV:") != NULL)
		events |= MQTT_EVT_RECV;
	if (strstr(aRxBuffer, "+QMTSTAT:") != NULL)
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
