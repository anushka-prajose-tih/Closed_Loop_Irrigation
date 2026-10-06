"""Simulated field device for testing main.py against the local Mosquitto broker.

Publishes a sensor reading on `data` every interval, runs the motor when a
start command arrives on `motor_cmd`, and reports "on"/"off" on `motor_status`
(retained, so the broker always holds the latest motor state). Commands on
`ota` and `cmd` are logged; `cmd: reset` also stops the motor.
"""

import argparse
import json
import random
import threading
import time

import paho.mqtt.client as mqtt

BROKER_HOST = "localhost"
BROKER_PORT = 1883

TOPIC_DATA = "data"
TOPIC_MOTOR_CMD = "motor_cmd"
TOPIC_MOTOR_STATUS = "motor_status"
TOPIC_OTA = "ota"
TOPIC_CMD = "cmd"

DEFAULT_DEVICE_ID = "SASYAM_1A2B3C4D"


class Device:
    def __init__(self, client, device_id, tti_unit_seconds, soil_override):
        self.client = client
        self.device_id = device_id
        self.tti_unit_seconds = tti_unit_seconds
        self.soil_override = soil_override
        self.motor_timer = None
        self.lock = threading.Lock()

    def moisture(self):
        if self.soil_override is not None:
            return self.soil_override
        return round(random.uniform(5, 40), 1)

    def reading(self):
        return {
            "timestamp": int(time.time()),
            "Device_ID": self.device_id,
            "rssi": random.randint(-100, -50),
            "battery_volt": 0,
            "soil": {
                "SM1": self.moisture(), "ST1": round(random.uniform(20, 32), 1),
                "SEC1": random.randint(100, 900),
                "N1": random.randint(10, 60), "P1": random.randint(5, 40),
                "K1": random.randint(20, 120), "pH1": round(random.uniform(5.5, 7.5), 1),
                "LW1": random.randint(0, 100),
                "SM2": self.moisture(), "ST2": round(random.uniform(20, 32), 1),
                "SEC2": random.randint(100, 900),
                "N2": 0, "P2": 0, "K2": 0, "pH2": 0, "LW2": 0,
            },
            "weather": {
                "Tw": round(random.uniform(22, 38), 1), "RHw": random.randint(30, 90),
                "BP": round(random.uniform(1000, 1020), 1), "RF": round(random.uniform(0, 5), 1),
                "WS": round(random.uniform(0, 8), 1), "WD": random.randint(0, 359),
                "SR": random.randint(0, 1000),
            },
            "canopy": {
                "Tc1": round(random.uniform(22, 36), 1), "RHc1": random.randint(30, 90),
                "CO2c1": random.randint(380, 600),
                "Tc2": 0, "RHc2": 0, "CO2c2": 0,
                "Tc3": 0, "RHc3": 0, "CO2c3": 0,
                "O2c": 0, "SR": random.randint(0, 1000), "UV": 0, "PAR": 0,
                "Tb": 0, "BD": 0, "FD": 0, "LAI": 0, "NDVI": round(random.uniform(0, 5), 1),
            },
        }

    def publish_reading(self):
        reading = self.reading()
        self.client.publish(TOPIC_DATA, json.dumps(reading), qos=1)
        soil = reading["soil"]
        print(f"Published reading: SM1={soil['SM1']} SM2={soil['SM2']}")

    def publish_status(self, status):
        message = {"timestamp": int(time.time()), "Device_ID": self.device_id, "status": status}
        self.client.publish(TOPIC_MOTOR_STATUS, json.dumps(message), qos=1, retain=True)
        print(f"Motor {status}")

    def start_motor(self, tti):
        with self.lock:
            if self.motor_timer:
                self.motor_timer.cancel()
            duration = tti * self.tti_unit_seconds
            self.motor_timer = threading.Timer(duration, self.stop_motor)
            self.motor_timer.daemon = True
            self.motor_timer.start()
        self.publish_status("on")
        print(f"Running for TTI {tti} ({duration:g}s)")

    def stop_motor(self):
        with self.lock:
            if self.motor_timer:
                self.motor_timer.cancel()
            self.motor_timer = None
        self.publish_status("off")

    def handle(self, topic, message):
        if message.get("Device_ID") != self.device_id:
            return
        if topic == TOPIC_MOTOR_CMD:
            cmd = message.get("cmd")
            if cmd == "start":
                try:
                    tti = int(str(message.get("tti")).strip())
                except ValueError:
                    print(f"Bad tti in motor_cmd: {message.get('tti')!r}")
                    return
                self.start_motor(max(0, min(255, tti)))
            elif cmd == "stop":
                self.stop_motor()
        elif topic == TOPIC_CMD:
            print(f"Received cmd: {message.get('cmd')}")
            if message.get("cmd") == "reset":
                self.stop_motor()
        elif topic == TOPIC_OTA:
            print(f"Received OTA: version={message.get('version')} "
                  f"size={message.get('size')} url={message.get('url')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--device-id", default=DEFAULT_DEVICE_ID)
    parser.add_argument("--interval", type=float, default=3600,
                        help="seconds between sensor readings (default 3600)")
    parser.add_argument("--tti-unit-seconds", type=float, default=60,
                        help="seconds of motor run time per TTI unit (default 60)")
    parser.add_argument("--soil", type=float,
                        help="fixed SM1/SM2 value instead of random 5-40")
    args = parser.parse_args()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    device = Device(client, args.device_id, args.tti_unit_seconds, args.soil)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"MQTT connection failed: {reason_code}")
            return
        client.subscribe([(TOPIC_MOTOR_CMD, 1), (TOPIC_OTA, 1), (TOPIC_CMD, 1)])

    def on_message(client, userdata, msg):
        try:
            message = json.loads(msg.payload)
        except ValueError:
            print(f"Ignoring non-JSON message on '{msg.topic}'.")
            return
        device.handle(msg.topic, message)

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER_HOST, BROKER_PORT)
    client.loop_start()
    print(f"Device {args.device_id} connected to {BROKER_HOST}:{BROKER_PORT}.")

    device.publish_status("off")
    try:
        while True:
            device.publish_reading()
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
