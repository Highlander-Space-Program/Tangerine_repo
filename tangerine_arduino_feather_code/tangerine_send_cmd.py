"""
tangerine_send_cmd.py

Send a single-byte command to the Tangerine board over MQTT.
Requires: pip install paho-mqtt

Usage:
    python tangerine_send_cmd.py <command_name_or_hex>

Examples:
    python tangerine_send_cmd.py arm_igniter
    python tangerine_send_cmd.py disarm_servos
    python tangerine_send_cmd.py 0x0A
    python tangerine_send_cmd.py 10

Also subscribes to device/breakwire and tangerine/status to print board responses.

Board IP: 172.27.160.50 (static, set in firmware)
MQTT broker: 172.27.160.1:1883 (PC ethernet adapter IP — direct link, no router)
"""

import sys
import time
import paho.mqtt.client as mqtt

# ----- Config -----
BROKER_IP   = "192.168.100.1"  # PC's ethernet adapter IP (direct link to board)
BROKER_PORT = 1883

CMD_TOPIC       = "device/command"
BREAKWIRE_TOPIC = "device/breakwire"
STATUS_TOPIC    = "tangerine/status"

# Command name -> byte value (must match gui_msg_t enum in mqtt_task.h)
COMMANDS = {
    "arm_igniter":          0x00,
    "disarm_igniter":       0x01,
    "arm_auto_ignition":    0x02,
    "disarm_auto_ignition": 0x03,
    "arm_servos":           0x04,
    "disarm_servos":        0x05,

    "open_servo_1":         0x06,
    "close_servo_1":        0x07,
    "open_servo_2":         0x08,
    "close_servo_2":        0x09,
    "open_servo_3":         0x0A,
    "close_servo_3":        0x0B,

    "igniter_fire":         0x0C,
    "igniter_off":          0x0D,

    "led_off":              0x0E,
    "led_medium":           0x0F,
}
# ------------------


def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected to broker ({reason_code})")
    client.subscribe(BREAKWIRE_TOPIC)
    client.subscribe(STATUS_TOPIC)
    print(f"[MQTT] Subscribed to {BREAKWIRE_TOPIC} and {STATUS_TOPIC}")


def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
    except UnicodeDecodeError:
        payload = msg.payload.hex()
    print(f"[{msg.topic}] {payload}")


def parse_command(arg: str) -> int:
    # Named command
    if arg.lower() in COMMANDS:
        return COMMANDS[arg.lower()]
    # Hex or decimal integer
    try:
        return int(arg, 0)  # int("0x0A", 0) or int("10", 0) both work
    except ValueError:
        return -1


def print_commands():
    print("Available named commands:")
    for name, byte in COMMANDS.items():
        print(f"  {name:<25} 0x{byte:02X}")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        print_commands()
        sys.exit(0)

    cmd_byte = parse_command(sys.argv[1])
    if cmd_byte < 0 or cmd_byte > 0xFF:
        print(f"Unknown command: {sys.argv[1]}")
        print_commands()
        sys.exit(1)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {BROKER_IP}:{BROKER_PORT} ...")
    client.connect(BROKER_IP, BROKER_PORT, keepalive=10)
    client.loop_start()

    time.sleep(0.5)  # wait for on_connect

    print(f"[MQTT] Publishing 0x{cmd_byte:02X} to {CMD_TOPIC}")
    client.publish(CMD_TOPIC, payload=bytes([cmd_byte]), qos=0)

    # Wait a moment to catch any board response on status/breakwire topics
    time.sleep(2)

    client.loop_stop()
    client.disconnect()
