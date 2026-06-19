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

// TODO: calibrate these for servo 1 (fill valve)
#define FILL_VALVE_OPEN_DC  200
#define FILL_VALVE_CLOSE_DC 50

// Network config — TODO: update for your actual network
#define MQTT_CLIENT_ID   "tangerine-gic-01"
#define MQTT_PORT        1883

// Topics — must match the DAQ server (server/mqtt.py ControlPublisher)
// GUI sends a single gui_msg_t byte to MQTT_CMD_TOPIC
// Board publishes general status strings to MQTT_STATUS_TOPIC
// Board publishes breakwire state to MQTT_BREAKWIRE_TOPIC (plain text: "broken" / "connected")
#define MQTT_CMD_TOPIC       "device/command"
#define MQTT_STATUS_TOPIC    "tangerine/status"
#define MQTT_BREAKWIRE_TOPIC "device/breakwire"

// GUI message command enum — single byte sent over MQTT from the GUI to the board
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


// Global command queue — filled by mqttCallback, drained by vmqtt_cmd_processor_task
// (mirrors xcan_rx_queue_handle from OPC main.c)
extern QueueHandle_t xmqtt_cmd_queue;

// Mutex protecting all mqtt.publish() / mqtt.loop() calls across tasks
extern SemaphoreHandle_t xmqtt_mutex;

extern TaskHandle_t vmqtt_task_handle;
extern TaskHandle_t vmqtt_cmd_processor_task_handle;

// MQTT callback — called by PubSubClient inside mqtt.loop() when a message arrives
// Puts the command byte into xmqtt_cmd_queue for vmqtt_cmd_processor_task to handle
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Handles MQTT connection, reconnection, and calls mqtt.loop() every 10ms
void vmqtt_task(void *pvParameters);

// Reads commands from xmqtt_cmd_queue and routes them to the correct handler
// (equivalent of vprocess_can_rx_msg + all class handlers from OPC main.c)
void vmqtt_cmd_processor_task(void *pvParameters);

// Publishes a status string to MQTT_STATUS_TOPIC
// Thread-safe via xmqtt_mutex — safe to call from any task
void publishStatus(const char* msg);

// Publishes breakwire state to MQTT_BREAKWIRE_TOPIC ("broken" or "connected")
// DAQ server reads this topic to update the controls page breakwire indicator
void publishBreakwire(const char* status);
