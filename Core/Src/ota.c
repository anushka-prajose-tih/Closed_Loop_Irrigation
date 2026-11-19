/**
 ******************************************************************************
 * @file    ota.c
 * @brief   Firmware update over the air from ThingsBoard (device HTTP API)
 *
 * Flash layout: the STM32G0B1RE has two 256 KB banks. The running image is
 * always mapped at 0x08000000 and the other bank at 0x08040000, whichever
 * physical bank is active (the nSWAP_BANK option bit decides). The app is
 * linked for one bank only (STM32G0B1RETX_FLASH.ld: FLASH LENGTH = 256K).
 *
 * On every boot OTA_Poll() reads the device's shared attributes, which
 * describe the firmware assigned to it in ThingsBoard:
 *   GET /api/v1/<token>/attributes?sharedKeys=fw_title,fw_version,...
 * ThingsBoard leaves the "is this an update?" decision to the device. Here
 * it is made by hashing the running image: if its SHA-256 over fw_size bytes
 * equals fw_checksum, the device already runs that firmware. Comparing hashes
 * rather than version strings means a build whose version constant wasn't
 * bumped can never cause an endless download-install loop.
 *
 * Otherwise the image is downloaded into the modem's file system
 * (GET /api/v1/<token>/firmware?title=..&version=..), copied into the
 * inactive bank, checked against fw_checksum from flash, and the banks are
 * swapped by flipping nSWAP_BANK (the option-byte reload resets the MCU into
 * the new image). Progress goes to ThingsBoard as the fw_state telemetry its
 * OTA dashboard expects: DOWNLOADING, DOWNLOADED, VERIFIED, UPDATING, then
 * UPDATED (with current_fw_title/current_fw_version) or FAILED (with
 * fw_error), via POST /api/v1/<token>/telemetry.
 *
 * The new image is on trial until it reaches the server again, which shows
 * it can still receive the next fix. OTA_BootCheck() counts its boots in the
 * RTC backup registers; after OTA_MAX_TRIAL_BOOTS unconfirmed boots it swaps
 * back and remembers the failed image's checksum, so the old image doesn't
 * download it again. Hung images are caught by the watchdog (IWDG, when
 * enabled in the .ioc), whose reset counts as a boot.
 *
 * Flash erase works on physical banks regardless of nSWAP_BANK (RM0444), so
 * the bank to erase comes from nSWAP_BANK as read at boot, never a constant.
 ******************************************************************************
 */

#include "ota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern RTC_HandleTypeDef hrtc;
extern char print_char[200];

#define OTA_FILE              "fw.bin"
#define OTA_RUNNING_ADDR      0x08000000U
#define OTA_INACTIVE_ADDR     0x08040000U
#define OTA_BANK_SIZE         0x40000U
#define OTA_PAGES_PER_BANK    (OTA_BANK_SIZE / FLASH_PAGE_SIZE)
#define OTA_CHUNK_SIZE        1024U
#define OTA_RAM_START         0x20000000U
#define OTA_RAM_END           0x20024000U   /* 144 KB */
#define OTA_HTTP_TIMEOUT_S    60U

/* RTC backup registers, which survive resets (not power loss without VBAT):
   DR0 = magic << 16 | state << 8 | boot count, DR1 unused,
   DR2 = first 4 bytes of the trial image's SHA-256, DR3 = its size,
   DR4 = first 4 bytes of the SHA-256 of an image that failed (don't retry it) */
#define OTA_MAGIC             0x0A7AU
#define OTA_STATE_NONE        0U
#define OTA_STATE_TRIAL       1U   /* new image running, not yet confirmed */
#define OTA_STATE_ROLLED_BACK 2U   /* swapped back; failure not yet reported */

typedef struct
{
	uint32_t h[8];
	uint8_t buf[64];
	uint32_t bufLen;
	uint32_t totalLen;
} Sha256;

static uint32_t inactiveBank;   /* physical bank behind 0x08040000, read at boot */
static uint8_t polledThisBoot;
static uint8_t otaDisabled;
static char host[64];           /* Host header, from OTA_SERVER_URL */
static char url[256];
static char request[640];       /* sent by interrupt: must outlive the call */
static char body[1536];
static uint8_t chunk[OTA_CHUNK_SIZE];

/* ------------------------------------------------------------------ SHA-256 */

static const uint32_t K[64] = {
	0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
	0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
	0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
	0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
	0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
	0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
	0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
	0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32U - (n))))

static void sha256_block(Sha256 *s, const uint8_t *p)
{
	uint32_t w[64];
	uint32_t a, b, c, d, e, f, g, h, t1, t2;
	uint32_t i;

	for (i = 0; i < 16U; i++)
	{
		w[i] = ((uint32_t)p[4U * i] << 24) | ((uint32_t)p[4U * i + 1U] << 16) |
			   ((uint32_t)p[4U * i + 2U] << 8) | (uint32_t)p[4U * i + 3U];
	}
	for (i = 16; i < 64U; i++)
	{
		uint32_t s0 = ROTR(w[i - 15U], 7U) ^ ROTR(w[i - 15U], 18U) ^ (w[i - 15U] >> 3);
		uint32_t s1 = ROTR(w[i - 2U], 17U) ^ ROTR(w[i - 2U], 19U) ^ (w[i - 2U] >> 10);
		w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
	}

	a = s->h[0];
	b = s->h[1];
	c = s->h[2];
	d = s->h[3];
	e = s->h[4];
	f = s->h[5];
	g = s->h[6];
	h = s->h[7];
	for (i = 0; i < 64U; i++)
	{
		t1 = h + (ROTR(e, 6U) ^ ROTR(e, 11U) ^ ROTR(e, 25U)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROTR(a, 2U) ^ ROTR(a, 13U) ^ ROTR(a, 22U)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
	s->h[5] += f;
	s->h[6] += g;
	s->h[7] += h;
}

static void sha256_init(Sha256 *s)
{
	static const uint32_t h0[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
								   0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

	memcpy(s->h, h0, sizeof(h0));
	s->bufLen = 0;
	s->totalLen = 0;
}

static void sha256_update(Sha256 *s, const uint8_t *data, uint32_t len)
{
	uint32_t i;

	for (i = 0; i < len; i++)
	{
		s->buf[s->bufLen++] = data[i];
		if (s->bufLen == 64U)
		{
			sha256_block(s, s->buf);
			s->bufLen = 0;
		}
	}
	s->totalLen += len;
}

static void sha256_final(Sha256 *s, uint8_t out[32])
{
	uint64_t bits = (uint64_t)s->totalLen * 8U;
	uint32_t i;

	s->buf[s->bufLen++] = 0x80U;
	if (s->bufLen > 56U)
	{
		while (s->bufLen < 64U)
			s->buf[s->bufLen++] = 0U;
		sha256_block(s, s->buf);
		s->bufLen = 0;
	}
	while (s->bufLen < 56U)
		s->buf[s->bufLen++] = 0U;
	for (i = 0; i < 8U; i++)
		s->buf[56U + i] = (uint8_t)(bits >> (56U - 8U * i));
	sha256_block(s, s->buf);
	for (i = 0; i < 8U; i++)
	{
		out[4U * i] = (uint8_t)(s->h[i] >> 24);
		out[4U * i + 1U] = (uint8_t)(s->h[i] >> 16);
		out[4U * i + 2U] = (uint8_t)(s->h[i] >> 8);
		out[4U * i + 3U] = (uint8_t)s->h[i];
	}
}

/* SHA-256 of len bytes of memory, as 64 lowercase hex digits; also returns
   the first 4 bytes as a word in *prefix if it isn't NULL */
static void sha256_hex(const uint8_t *data, uint32_t len, char hex[65], uint32_t *prefix)
{
	Sha256 s;
	uint8_t digest[32];
	uint32_t done = 0;
	uint32_t i;

	sha256_init(&s);
	while (done < len)
	{
		uint32_t n = (len - done > 4096U) ? 4096U : (len - done);
		sha256_update(&s, data + done, n);
		done += n;
		App_WatchdogRefresh();
	}
	sha256_final(&s, digest);
	for (i = 0; i < 32U; i++)
		snprintf(&hex[2U * i], 3, "%02x", digest[i]);
	if (prefix != NULL)
		*prefix = ((uint32_t)digest[0] << 24) | ((uint32_t)digest[1] << 16) |
				  ((uint32_t)digest[2] << 8) | (uint32_t)digest[3];
}

/* Known-answer test, so a broken hash can never approve an image */
static uint8_t sha256_selfTest(void)
{
	char hex[65];

	sha256_hex((const uint8_t *)"abc", 3U, hex, NULL);
	return strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0;
}

/* ------------------------------------------------------- state in backup RAM */

static void _setState(uint8_t state, uint8_t boots, uint32_t shaPrefix, uint32_t size)
{
	HAL_PWR_EnableBkUpAccess();
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, ((uint32_t)OTA_MAGIC << 16) | ((uint32_t)state << 8) | boots);
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, shaPrefix);
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, size);
}

/* An image (by SHA-256 prefix) that failed to install or confirm: the same
   assignment is not retried until a different firmware is assigned */
static void _setFailed(uint32_t shaPrefix)
{
	HAL_PWR_EnableBkUpAccess();
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR4, shaPrefix);
}

static uint8_t _getState(uint8_t *boots)
{
	uint32_t v = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);

	if ((v >> 16) != OTA_MAGIC)
		return OTA_STATE_NONE;
	*boots = (uint8_t)v;
	return (uint8_t)(v >> 8);
}

/* ------------------------------------------------------------ flash helpers */

static uint8_t _isBlank(uint32_t addr, uint32_t len)
{
	const volatile uint32_t *p = (const volatile uint32_t *)addr;
	uint32_t i;

	for (i = 0; i < len / 4U; i++)
	{
		if (p[i] != 0xFFFFFFFFU)
			return RESET;
	}
	return SET;
}

/* Plausible vector table for an image linked at 0x08000000: initial stack
   pointer in RAM, Thumb reset handler inside one bank */
static uint8_t _imageLooksValid(uint32_t base)
{
	uint32_t sp = *(const volatile uint32_t *)base;
	uint32_t reset = *(const volatile uint32_t *)(base + 4U);

	return (sp > OTA_RAM_START && sp <= OTA_RAM_END && (reset & 1U) != 0U &&
			reset >= OTA_RUNNING_ADDR && reset < OTA_RUNNING_ADDR + OTA_BANK_SIZE)
			   ? SET
			   : RESET;
}

static uint8_t _erasePage(uint32_t page)
{
	FLASH_EraseInitTypeDef erase = {0};
	uint32_t pageError = 0;

	erase.TypeErase = FLASH_TYPEERASE_PAGES;
	erase.Banks = inactiveBank; /* physical bank: erase ignores nSWAP_BANK */
	erase.Page = page;
	erase.NbPages = 1U;
	App_WatchdogRefresh();
	return (HAL_FLASHEx_Erase(&erase, &pageError) == HAL_OK) ? SET : RESET;
}

/* Proves, before erasing for real, that a page erase with the bank chosen
   from nSWAP_BANK lands at 0x08040000 on this chip (RM0444 numbering, the
   HAL's page numbering within the bank, and the swap logic all have to
   agree). It uses the last page of the bank, which is unused in the running
   image too, so even an erase that went to the wrong bank harms nothing. */
static uint8_t _eraseProbe(void)
{
	const uint32_t page = OTA_PAGES_PER_BANK - 1U;
	const uint32_t target = OTA_INACTIVE_ADDR + page * FLASH_PAGE_SIZE;

	if (!_isBlank(OTA_RUNNING_ADDR + page * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE))
	{
		serial_print("OTA: last page of the running bank is in use, image too large for the probe");
		return RESET;
	}
	if (_isBlank(target, FLASH_PAGE_SIZE))
	{
		/* Give the erase something to remove */
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, target, 0x5A5A5A5AA5A5A5A5ULL) != HAL_OK ||
			_isBlank(target, FLASH_PAGE_SIZE))
		{
			serial_print("OTA: probe write did not land at 0x08040000");
			return RESET;
		}
	}
	if (!_erasePage(page) || !_isBlank(target, FLASH_PAGE_SIZE))
	{
		serial_print("OTA: probe erase did not clear 0x08040000 - bank/page numbering mismatch");
		return RESET;
	}
	return SET;
}

/* Writes len bytes at addr in 8-byte double words, padding the tail with 0xFF */
static uint8_t _program(uint32_t addr, const uint8_t *data, uint32_t len)
{
	uint32_t i;

	for (i = 0; i < len; i += 8U)
	{
		uint64_t dw = 0xFFFFFFFFFFFFFFFFULL;
		memcpy(&dw, &data[i], (len - i >= 8U) ? 8U : (len - i));
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + i, dw) != HAL_OK)
			return RESET;
	}
	return SET;
}

/* Flips nSWAP_BANK (active-low) and reloads the option bytes, which resets
   the MCU into the other bank. Only the user bits are written: the HAL keeps
   the current readout-protection level. Returns only on failure. */
static void _swapBanksAndReset(void)
{
	FLASH_OBProgramInitTypeDef ob = {0};

	ob.OptionType = OPTIONBYTE_USER;
	ob.USERType = OB_USER_BANK_SWAP;
	ob.USERConfig = (inactiveBank == FLASH_BANK_2) ? OB_USER_DUALBANK_SWAP_ENABLE : OB_USER_DUALBANK_SWAP_DISABLE;

	HAL_FLASH_Unlock();
	HAL_FLASH_OB_Unlock();
	if (HAL_FLASHEx_OBProgram(&ob) == HAL_OK)
		HAL_FLASH_OB_Launch(); /* resets */
	HAL_FLASH_OB_Lock();
	HAL_FLASH_Lock();
	serial_print("OTA: option byte programming failed, banks not swapped");
}

/* ------------------------------------------------------- ThingsBoard (HTTP) */

/* Copies the string value of "key" (first match after `from`) into out */
static uint8_t _jsonString(const char *from, const char *key, char *out, size_t outSize)
{
	char pattern[32];
	const char *p;
	const char *end;

	if (from == NULL)
		return RESET;
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(from, pattern);
	if (p == NULL)
		return RESET;
	p += strlen(pattern);
	while (*p == ' ' || *p == ':')
		p++;
	if (*p != '"')
		return RESET;
	p++;
	end = strchr(p, '"');
	if (end == NULL || (size_t)(end - p) >= outSize)
		return RESET;
	memcpy(out, p, (size_t)(end - p));
	out[end - p] = '\0';
	return SET;
}

static long _jsonNumber(const char *from, const char *key)
{
	char pattern[32];
	const char *p;

	if (from == NULL)
		return -1;
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(from, pattern);
	if (p == NULL)
		return -1;
	p += strlen(pattern);
	while (*p == ' ' || *p == ':')
		p++;
	return strtol(p, NULL, 10);
}

/* Percent-encodes everything except RFC 3986 unreserved characters */
static void _urlEncode(const char *src, char *dst, size_t dstSize)
{
	size_t n = 0;

	for (; *src != '\0' && n + 4U < dstSize; src++)
	{
		char c = *src;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '.' || c == '_' || c == '~')
		{
			dst[n++] = c;
		}
		else
		{
			n += (size_t)snprintf(&dst[n], dstSize - n, "%%%02X", (unsigned char)c);
		}
	}
	dst[n] = '\0';
}

/* One request to the server. `path` starts with "/api/v1/<token>", so it is
   never logged: `label` names the request in the log instead. Returns the
   HTTP status, or -1. */
static long _request(const char *method, const char *path, const char *accept, const char *json, const char *label)
{
	int n;
	long status;

	snprintf(url, sizeof(url), "%s%s", OTA_SERVER_URL, path);
	n = snprintf(request, sizeof(request),
				 "%s %s HTTP/1.1\r\nHost: %s\r\nAccept: %s\r\nConnection: close\r\n",
				 method, path, host, accept);
	if (n > 0 && (size_t)n < sizeof(request))
	{
		if (json != NULL)
			n += snprintf(&request[n], sizeof(request) - (size_t)n,
						  "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s",
						  (unsigned int)strlen(json), json);
		else
			n += snprintf(&request[n], sizeof(request) - (size_t)n, "\r\n");
	}
	if (n <= 0 || (size_t)n >= sizeof(request))
	{
		serial_print("OTA: request too long");
		return -1;
	}

	status = GSM_HTTP_rawRequest((json != NULL) ? 1U : 0U, url, request, OTA_HTTP_TIMEOUT_S);
	snprintf(print_char, sizeof(print_char), "OTA: %s %s -> %ld", method, label, status);
	serial_print(print_char);
	return status;
}

/* fw_state telemetry for ThingsBoard's OTA dashboard */
static void _telemetry(const char *json)
{
	char path[64];

	snprintf(path, sizeof(path), "/api/v1/%s/telemetry", OTA_ACCESS_TOKEN);
	(void)_request("POST", path, "application/json", json, "telemetry");
}

static void _state(const char *fwState)
{
	char json[48];

	snprintf(json, sizeof(json), "{\"fw_state\":\"%s\"}", fwState);
	_telemetry(json);
}

/* Reports FAILED with the reason. A failure caused by the image itself
   (shaPrefix != 0) is remembered, so the same assignment isn't retried on
   every boot; network failures are retried on the next boot. */
static void _fail(const char *reason, uint32_t shaPrefix)
{
	char json[160];

	snprintf(print_char, sizeof(print_char), "OTA: update failed: %s", reason);
	serial_print(print_char);
	if (shaPrefix != 0U)
		_setFailed(shaPrefix);
	snprintf(json, sizeof(json), "{\"fw_state\":\"FAILED\",\"fw_error\":\"%s\"}", reason);
	_telemetry(json);
}

/* Downloads, writes, verifies and switches to firmware title/version.
   Returns only if the update didn't happen. */
static void _install(const char *title, const char *version, uint32_t size, const char *checksum, uint32_t prefix)
{
	char path[256];
	char encTitle[96];
	char encVersion[64];
	char actual[65];
	long fh;
	int32_t n;
	uint32_t written = 0;
	const char *reason = NULL;
	uint32_t pages;
	uint32_t page;

	snprintf(print_char, sizeof(print_char), "OTA: updating to %.40s %.20s, %lu bytes", title, version, size);
	serial_print(print_char);

	/* Download into the modem first, so a dropped connection never leaves a
	   half-written bank */
	_state("DOWNLOADING");
	_urlEncode(title, encTitle, sizeof(encTitle));
	_urlEncode(version, encVersion, sizeof(encVersion));
	snprintf(path, sizeof(path), "/api/v1/%s/firmware?title=%s&version=%s", OTA_ACCESS_TOKEN, encTitle, encVersion);
	GSM_fileDelete(OTA_FILE);
	if (_request("GET", path, "application/octet-stream", NULL, "firmware") != 200 ||
		!GSM_HTTP_readToFile(OTA_FILE))
	{
		GSM_fileDelete(OTA_FILE);
		_fail("Download failed", 0U);
		return;
	}
	_state("DOWNLOADED");
	fh = GSM_fileOpen(OTA_FILE);
	if (fh < 0)
	{
		GSM_fileDelete(OTA_FILE);
		_fail("Could not open the downloaded file", 0U);
		return;
	}

	HAL_FLASH_Unlock();
	if (!_eraseProbe())
	{
		HAL_FLASH_Lock();
		GSM_fileClose(fh);
		GSM_fileDelete(OTA_FILE);
		_fail("Flash erase check failed on the device", prefix);
		return;
	}
	pages = (size + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE;
	for (page = 0; page < pages; page++)
	{
		if (!_erasePage(page))
		{
			HAL_FLASH_Lock();
			GSM_fileClose(fh);
			GSM_fileDelete(OTA_FILE);
			_fail("Flash erase failed", 0U);
			return;
		}
	}

	/* Read exactly fw_size bytes and stop: the EC200U's reply to a read at
	   the end of the file isn't the usual "CONNECT <n> ... OK" framing, so
	   asking for one made a complete copy look failed (field log, Nov 2026) */
	while (written < size && reason == NULL)
	{
		uint16_t want = (size - written > OTA_CHUNK_SIZE) ? (uint16_t)OTA_CHUNK_SIZE : (uint16_t)(size - written);

		n = GSM_fileRead(fh, chunk, want);
		if (n <= 0)
			reason = "Reading the downloaded file failed (shorter than fw_size?)";
		else if (!_program(OTA_INACTIVE_ADDR + written, chunk, (uint32_t)n))
			reason = "Flash programming failed";
		else
			written += (uint32_t)n;
	}
	HAL_FLASH_Lock();
	GSM_fileClose(fh);
	GSM_fileDelete(OTA_FILE);
	if (reason != NULL)
	{
		snprintf(print_char, sizeof(print_char), "OTA: copied %lu of %lu bytes", written, size);
		serial_print(print_char);
		_fail(reason, 0U);
		return;
	}

	/* Check what actually ended up in flash, not what was received */
	sha256_hex((const uint8_t *)OTA_INACTIVE_ADDR, size, actual, NULL);
	if (strcmp(actual, checksum) != 0)
	{
		_fail("SHA-256 mismatch after writing to flash", prefix);
		return;
	}
	if (!_imageLooksValid(OTA_INACTIVE_ADDR))
	{
		_fail("Not a firmware image for this board (bad vector table)", prefix);
		return;
	}
	_state("VERIFIED");

	_state("UPDATING");
	_setState(OTA_STATE_TRIAL, 0U, prefix, size);
	serial_print("OTA: image verified, swapping banks and restarting");
	/* The new image's boot path power-cycles the modem (App_operation()) */
	_swapBanksAndReset();
	_setState(OTA_STATE_NONE, 0U, 0U, 0U);
	serial_print("OTA: update failed: Could not switch flash banks");
}

/* ------------------------------------------------------------------- public */

void OTA_BootCheck(void)
{
	uint8_t boots = 0;
	uint8_t state;
	uint32_t prefix;
	uint32_t trialPrefix;
	uint32_t trialSize;
	const char *h;
	char hex[65];
	size_t len;

	/* nSWAP_BANK = 1: no swap, bank 1 runs, physical bank 2 is behind
	   0x08040000. Read once here: a pending option-byte change would show in
	   OPTR before it takes effect. */
	inactiveBank = ((FLASH->OPTR & FLASH_OPTR_nSWAP_BANK) != 0U) ? FLASH_BANK_2 : FLASH_BANK_1;
	snprintf(print_char, sizeof(print_char), "OTA: running from flash bank %u",
			 (inactiveBank == FLASH_BANK_2) ? 1U : 2U);
	serial_print(print_char);

	/* Host header: OTA_SERVER_URL without the scheme */
	h = strstr(OTA_SERVER_URL, "://");
	h = (h != NULL) ? h + 3 : OTA_SERVER_URL;
	len = strcspn(h, "/");
	if (len >= sizeof(host))
		len = sizeof(host) - 1U;
	memcpy(host, h, len);
	host[len] = '\0';

	if (!sha256_selfTest())
	{
		otaDisabled = 1U;
		serial_print("OTA: SHA-256 self-test failed, OTA disabled");
		return;
	}

	state = _getState(&boots);
	if (state != OTA_STATE_TRIAL)
		return;

	/* Make sure the trial state belongs to the image actually running, not
	   one replaced since (e.g. reflashed with the debugger) */
	trialPrefix = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
	trialSize = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
	if (trialSize == 0U || trialSize > OTA_BANK_SIZE)
	{
		serial_print("OTA: trial state is corrupt, cleared");
		_setState(OTA_STATE_NONE, 0U, 0U, 0U);
		return;
	}
	sha256_hex((const uint8_t *)OTA_RUNNING_ADDR, trialSize, hex, &prefix);
	if (prefix != trialPrefix)
	{
		serial_print("OTA: trial state is for a different image, cleared");
		_setState(OTA_STATE_NONE, 0U, 0U, 0U);
		return;
	}

	boots++;
	if (boots <= OTA_MAX_TRIAL_BOOTS)
	{
		_setState(OTA_STATE_TRIAL, boots, trialPrefix, trialSize);
		snprintf(print_char, sizeof(print_char), "OTA: new firmware, trial boot %u of %u", boots, OTA_MAX_TRIAL_BOOTS);
		serial_print(print_char);
		return;
	}

	serial_print("OTA: new firmware not confirmed, rolling back");
	if (!_imageLooksValid(OTA_INACTIVE_ADDR))
	{
		serial_print("OTA: previous firmware is not valid, staying on this one");
		_setState(OTA_STATE_NONE, 0U, 0U, 0U);
		return;
	}
	_setFailed(trialPrefix);
	_setState(OTA_STATE_ROLLED_BACK, 0U, 0U, 0U);
	_swapBanksAndReset();
	/* Swap failed: carry on here */
	_setState(OTA_STATE_TRIAL, boots, trialPrefix, trialSize);
}

void OTA_Poll(void)
{
	char path[160];
	char title[48];
	char version[32];
	char checksum[65];
	char algo[16];
	char running[65];
	char json[160];
	const char *shared;
	long size;
	uint8_t boots = 0;
	uint8_t state;
	uint32_t prefix;
	uint8_t i;

	if (polledThisBoot)
		return;
	polledThisBoot = 1U;
	if (otaDisabled || OTA_ACCESS_TOKEN[0] == '\0')
	{
		serial_print("OTA: not configured (OTA_ACCESS_TOKEN in ota_credentials.h)");
		return;
	}
	if (!GSM_netReady())
	{
		serial_print("OTA: network not ready, skipping update check");
		return;
	}

	snprintf(path, sizeof(path),
			 "/api/v1/%s/attributes?sharedKeys=fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm",
			 OTA_ACCESS_TOKEN);
	if (_request("GET", path, "application/json", NULL, "attributes") != 200 ||
		GSM_HTTP_readBody(body, sizeof(body)) < 0)
	{
		serial_print("OTA: update check failed");
		GSM_HTTP_endRaw();
		return;
	}
	serial_print(body);

	state = _getState(&boots);
	if (state == OTA_STATE_ROLLED_BACK)
	{
		_setState(OTA_STATE_NONE, 0U, 0U, 0U);
		snprintf(json, sizeof(json), "{\"fw_state\":\"FAILED\",\"fw_error\":\"New firmware not confirmed after %u boots; rolled back\"}",
				 OTA_MAX_TRIAL_BOOTS);
		_telemetry(json);
	}

	/* {"shared":{"fw_title":"..","fw_version":"..","fw_size":N,"fw_checksum":"..",
	   "fw_checksum_algorithm":"SHA256"}} - empty when no firmware is assigned */
	shared = strstr(body, "\"shared\"");
	size = _jsonNumber(shared, "fw_size");
	if (!_jsonString(shared, "fw_title", title, sizeof(title)) ||
		!_jsonString(shared, "fw_version", version, sizeof(version)) ||
		!_jsonString(shared, "fw_checksum", checksum, sizeof(checksum)) || size <= 0)
	{
		serial_print("OTA: no firmware assigned");
		if (state == OTA_STATE_TRIAL)
			_setState(OTA_STATE_NONE, 0U, 0U, 0U); /* reached the server: confirmed */
		GSM_HTTP_endRaw();
		return;
	}
	if (!_jsonString(shared, "fw_checksum_algorithm", algo, sizeof(algo)) || strcmp(algo, "SHA256") != 0)
	{
		_fail("Unsupported checksum algorithm (SHA256 only)", 0U);
		GSM_HTTP_endRaw();
		return;
	}
	for (i = 0; checksum[i] != '\0'; i++)
		checksum[i] = (char)((checksum[i] >= 'A' && checksum[i] <= 'F') ? (checksum[i] - 'A' + 'a') : checksum[i]);
	if (strlen(checksum) != 64U || (uint32_t)size > OTA_BANK_SIZE - FLASH_PAGE_SIZE)
	{
		_fail("Bad checksum, or firmware larger than a flash bank", 0U);
		GSM_HTTP_endRaw();
		return;
	}

	/* Already running the assigned firmware? Hash the running image over
	   fw_size bytes. This also confirms a new image on trial. */
	sha256_hex((const uint8_t *)OTA_RUNNING_ADDR, (uint32_t)size, running, NULL);
	if (strcmp(running, checksum) == 0)
	{
		if (state == OTA_STATE_TRIAL)
		{
			serial_print("OTA: new firmware confirmed");
			_setState(OTA_STATE_NONE, 0U, 0U, 0U);
		}
		else
		{
			serial_print("OTA: firmware is up to date");
		}
		snprintf(json, sizeof(json), "{\"fw_state\":\"UPDATED\",\"current_fw_title\":\"%s\",\"current_fw_version\":\"%s\"}",
				 title, version);
		_telemetry(json);
		GSM_HTTP_endRaw();
		return;
	}
	if (state == OTA_STATE_TRIAL)
	{
		/* Running an update other than the one now assigned: it reached the
		   server, so it is confirmed, and the new assignment proceeds */
		serial_print("OTA: new firmware confirmed");
		_setState(OTA_STATE_NONE, 0U, 0U, 0U);
	}

	memcpy(json, checksum, 8U); /* first 4 bytes, as in the backup registers */
	json[8] = '\0';
	prefix = (uint32_t)strtoul(json, NULL, 16);
	if (prefix == HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4))
	{
		serial_print("OTA: this firmware failed before; assign a different one to retry");
		GSM_HTTP_endRaw();
		return;
	}

	_install(title, version, (uint32_t)size, checksum, prefix);
	GSM_HTTP_endRaw();
}
