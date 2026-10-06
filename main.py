"""Irrigation controller: soil moisture readings over MQTT drive the motor.

Connects to Mosquitto, logs in to the Larsen Turbo API with an OTP, then waits
for hourly sensor readings on `data`. When soil moisture is 0-20 it fetches the
farmer's TTI (time to irrigate) and, if TTI is above 5, publishes a start
command on `motor_cmd`. The device reports the motor state on `motor_status`.
"""

import json
import queue
import sys
import time

import paho.mqtt.client as mqtt
import requests

API_BASE = "https://sasyam-ai.tihiitb.org/larsen-api/v1/larsen-turbo/farmer"
REQUEST_OTP_URL = f"{API_BASE}/auth/request-otp"
VERIFY_OTP_URL = f"{API_BASE}/auth/verify-otp"
FARMER_ME_URL = f"{API_BASE}/me"

MOBILE_NUMBER = "9099031104"
FCM_TOKEN = "string"
DEVICE_ID = "string"
DEVICE_INFO = "string"

BROKER_HOST = "localhost"
BROKER_PORT = 1883

TOPIC_DATA = "data"
TOPIC_MOTOR_CMD = "motor_cmd"
TOPIC_MOTOR_STATUS = "motor_status"

SOIL_MOISTURE_RANGE = (0, 20)
TTI_THRESHOLD = 5
TTI_KEY = "recommended_duration_minutes"

TIMEOUT = 30


class AuthError(Exception):
    pass


def request_otp():
    response = requests.post(
        REQUEST_OTP_URL, json={"mobileNumber": MOBILE_NUMBER}, timeout=TIMEOUT
    )
    response.raise_for_status()


def verify_otp(otp):
    """Return the JWT for a valid OTP, or None if the OTP was rejected."""
    response = requests.post(
        VERIFY_OTP_URL,
        json={
            "mobileNumber": MOBILE_NUMBER,
            "otp": otp,
            "fcmToken": FCM_TOKEN,
            "deviceId": DEVICE_ID,
            "deviceInfo": DEVICE_INFO,
        },
        timeout=TIMEOUT,
    )
    if not response.ok:
        return None
    try:
        payload = response.json()
    except ValueError:
        return None
    if str(payload.get("status")) != "200":
        return None
    return (payload.get("result") or {}).get("token")


def login():
    """Request and validate OTPs until one is accepted; return the JWT."""
    while True:
        request_otp()
        print(f"OTP sent to {MOBILE_NUMBER}.")
        otp = input("Enter OTP: ").strip()
        token = verify_otp(otp)
        if token:
            print("OTP verified, session token stored.")
            return token
        print("Invalid OTP, requesting a new one.")


def get_tti(token):
    response = requests.get(
        FARMER_ME_URL,
        headers={"Authorization": f"Bearer {token}"},
        timeout=TIMEOUT,
    )
    if response.status_code in (401, 403):
        raise AuthError("session token rejected")
    response.raise_for_status()
    payload = response.json()

    values = [v for v in find_values(payload, TTI_KEY) if v is not None]
    if not values:
        raise ValueError(f"{TTI_KEY} not found in /farmer/me response: {payload}")
    return float(values[0])


def find_values(data, key):
    """Collect every value stored under `key`, at any depth in the payload."""
    found = []
    if isinstance(data, dict):
        for k, v in data.items():
            if k == key:
                found.append(v)
            else:
                found.extend(find_values(v, key))
    elif isinstance(data, list):
        for item in data:
            found.extend(find_values(item, key))
    return found


def soil_moisture(reading):
    """The SM1 sensor value, or None if it's missing."""
    value = (reading.get("soil") or {}).get("SM1")
    return value if isinstance(value, (int, float)) else None


class Controller:
    def __init__(self, client):
        self.client = client
        self.token = None
        self.motor_status = {}  # Device_ID -> "on" / "off"

    def authenticate(self):
        self.token = login()

    def fetch_tti(self):
        try:
            return get_tti(self.token)
        except AuthError:
            print("Session expired, logging in again.")
            self.authenticate()
            return get_tti(self.token)

    def handle_status(self, status):
        device = status.get("Device_ID")
        state = status.get("status")
        self.motor_status[device] = state
        print(f"[{device}] motor {state}")

    def handle_data(self, reading):
        device = reading.get("Device_ID")
        moisture = soil_moisture(reading)
        if moisture is None:
            print(f"[{device}] no SM1 in reading, skipping.")
            return
        low, high = SOIL_MOISTURE_RANGE
        if not low <= moisture <= high:
            print(f"[{device}] soil moisture {moisture:g} not in {low}-{high}, skipping.")
            return
        if self.motor_status.get(device) == "on":
            print(f"[{device}] soil moisture {moisture:g} but motor already on, skipping.")
            return

        try:
            tti = self.fetch_tti()
        except (requests.RequestException, ValueError) as exc:
            print(f"[{device}] could not get TTI: {exc}", file=sys.stderr)
            return
        if tti <= TTI_THRESHOLD:
            print(f"[{device}] soil moisture {moisture:g}, TTI {tti:g} <= {TTI_THRESHOLD}, skipping.")
            return

        command = {
            "timestamp": int(time.time()),
            "Device_ID": device,
            "tti": str(max(0, min(255, round(tti)))),
            "cmd": "start",
        }
        self.client.publish(TOPIC_MOTOR_CMD, json.dumps(command), qos=1)
        print(f"[{device}] soil moisture {moisture:g}, TTI {tti:g}: published start.")


def main():
    messages = queue.Queue()

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"MQTT connection failed: {reason_code}", file=sys.stderr)
            return
        client.subscribe([(TOPIC_DATA, 1), (TOPIC_MOTOR_STATUS, 1)])

    def on_message(client, userdata, msg):
        # Handled on the main thread so HTTP calls and OTP prompts don't block
        # the MQTT network loop.
        messages.put((msg.topic, msg.payload))

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    try:
        client.connect(BROKER_HOST, BROKER_PORT)
    except OSError as exc:
        print(f"Could not connect to Mosquitto at {BROKER_HOST}:{BROKER_PORT}: {exc}", file=sys.stderr)
        return 1
    client.loop_start()
    print(f"Connected to Mosquitto at {BROKER_HOST}:{BROKER_PORT}.")

    controller = Controller(client)
    try:
        controller.authenticate()
    except requests.RequestException as exc:
        print(f"Login failed: {exc}", file=sys.stderr)
        client.loop_stop()
        return 1

    print(f"Waiting for readings on '{TOPIC_DATA}'. Ctrl+C to stop.")
    try:
        while True:
            topic, payload = messages.get()
            try:
                message = json.loads(payload)
            except ValueError:
                print(f"Ignoring non-JSON message on '{topic}'.", file=sys.stderr)
                continue
            if not isinstance(message, dict):
                continue
            if topic == TOPIC_MOTOR_STATUS:
                controller.handle_status(message)
            elif topic == TOPIC_DATA:
                controller.handle_data(message)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
