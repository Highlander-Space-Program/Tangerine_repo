#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

#include "led_task.h"
#include "breakwire_task.h"
#include "system_safety_flags.h"
#include "ignition_task.h"

// SPI1 pins — confirmed against RP2040 Feather pinout (SCK/MO/MI = 14/15/8)
// CS=10 is the standard Feather SPI CS pin used by the W5500 FeatherWing
#define SPI_SCK_PIN   14
#define SPI_MOSI_PIN  15
#define SPI_MISO_PIN  8
#define SPI_CS_PIN    10

// Servo 1 (fill valve) — servos 2 & 3 (run valves) defined in ignition_task.h
#define SERVO_1_PIN     1

// Pulled in from extremes to avoid stalling against mechanical stops (causes overheating).
#define FILL_VALVE_OPEN_US     1830
#define FILL_VALVE_CLOSE_US    1200

// TODO: verify open/close direction matches physical fill valve orientation
#define FILL_VALVE_OPEN_DC    SERVO_US_TO_DC(FILL_VALVE_OPEN_US)
#define FILL_VALVE_CLOSE_DC   SERVO_US_TO_DC(FILL_VALVE_CLOSE_US)

// Network config — TODO: update for your actual network
#define MQTT_CLIENT_ID   "tangerine-gic-01"
#define MQTT_PORT        1883

// Topics — must match the DAQ server (server/mqtt.py ControlPublisher)
//
// GUI sends a JSON command to MQTT_CMD_TOPIC, where "command" is a gui_msg_t number (below):
//     {"command": 4, "id": 1}
// Board answers every command on MQTT_RESPONSE_TOPIC, echoing the same id:
//     {"id": 1, "status": "Ok", "message": ""}
//     {"id": 1, "status": "Error", "message": "servos not armed"}
// If the command can't be read at all (bad JSON, missing id) the id is null:
//     {"id": null, "status": "Error", "message": "invalid json"}
//
// Board publishes things that happen on their own (not replies to a command) to MQTT_EVENT_TOPIC:
//     {"event": "auto_ignition", "status": "Ok", "message": "run valves opened"}
//     {"event": "auto_ignition", "status": "Error", "message": "aborted, safety flags cleared"}
//
// Board publishes breakwire state to MQTT_BREAKWIRE_TOPIC (single byte: 0x10 connected / 0x11 broken)
#define MQTT_CMD_TOPIC       "device/command"
#define MQTT_RESPONSE_TOPIC  "device/command/response"
#define MQTT_EVENT_TOPIC     "device/event"
#define MQTT_BREAKWIRE_TOPIC "device/breakwire"

// GUI message command enum — the "command" number sent over MQTT from the GUI to the board
typedef enum {

  // System flags
  GUI_MSG_ARM_IGNITER_SYSTEM    = 0x00,
  GUI_MSG_DISARM_IGNITER_SYSTEM = 0x01,

  GUI_MSG_ARM_AUTO_IGNITION     = 0x02,
  GUI_MSG_DISARM_AUTO_IGNITION  = 0x03,

  GUI_MSG_ARM_SERVOS            = 0x04,
  GUI_MSG_DISARM_SERVOS         = 0x05,

  // Servo Actuation
  GUI_MSG_OPEN_SERVO_1          = 0x06,
  GUI_MSG_CLOSE_SERVO_1         = 0x07,

  GUI_MSG_OPEN_SERVO_2          = 0x08,
  GUI_MSG_CLOSE_SERVO_2         = 0x09,

  GUI_MSG_OPEN_SERVO_3          = 0x0A,
  GUI_MSG_CLOSE_SERVO_3         = 0x0B,

  // Igniter
  GUI_MSG_IGNITER_FIRE          = 0x0C,
  GUI_MSG_IGNITER_OFF           = 0x0D,

  // LED Pinging
  GUI_MSG_LED_OFF               = 0x0E,
  GUI_MSG_LED_MEDIUM            = 0x0F,

  // Breakwire status (sent TO GUI via publishBreakwire, not received from GUI)
  GUI_MSG_BRKWIRE_CONNECTED     = 0x10,
  GUI_MSG_BRKWIRE_BROKEN        = 0x11
} gui_msg_t;

// One item in xmqtt_cmd_queue — the command plus the id the GUI sent with it,
// so the command processor can echo the id back in its response
typedef struct {
    uint8_t cmd;   // a gui_msg_t value
    long    id;
} mqtt_cmd_t;


// Global command queue — filled by mqttCallback, drained by vmqtt_cmd_processor_task
// (mirrors xcan_rx_queue_handle from OPC main.c)
extern QueueHandle_t xmqtt_cmd_queue;

// Mutex protecting all mqtt.publish() / mqtt.loop() calls.
// Only vmqtt_task uses the mqtt object now — other tasks go through publishStatus() / publishBreakwire()
extern SemaphoreHandle_t xmqtt_mutex;

extern TaskHandle_t vmqtt_task_handle;
extern TaskHandle_t vmqtt_cmd_processor_task_handle;

// MQTT callback — called by PubSubClient inside mqtt.loop() when a message arrives
// Parses the JSON command and puts it into xmqtt_cmd_queue for vmqtt_cmd_processor_task to handle
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Handles MQTT connection, reconnection, and calls mqtt.loop() every 10ms
void vmqtt_task(void *pvParameters);

// Reads commands from xmqtt_cmd_queue and routes them to the correct handler
// (equivalent of vprocess_can_rx_msg + all class handlers from OPC main.c)
void vmqtt_cmd_processor_task(void *pvParameters);

// Outgoing status messages — filled by publishStatus() from any task, drained by vmqtt_task.
// Each queue slot holds the topic and one null-terminated string, longer strings are cut to fit.
#define MQTT_STATUS_MSG_MAX_LEN  128
#define MQTT_STATUS_QUEUE_LEN    16

typedef struct {
    const char* topic; // always one of the MQTT_*_TOPIC string constants
    char text[MQTT_STATUS_MSG_MAX_LEN];
} mqtt_status_msg_t;

extern QueueHandle_t xmqtt_status_queue;

// Queues a message for vmqtt_task to publish on the given topic.
// Safe to call from any task — it never touches the mqtt object itself.
void publishStatus(const char* topic, const char* msg);

// Breakwire status for vmqtt_task to publish — set by breakwire_task, read by vmqtt_task.
// 0 = no update pending, 0x10 = connected, 0x11 = broken
extern volatile uint8_t g_breakwire_publish_byte;

void publishBreakwire(const char* status);