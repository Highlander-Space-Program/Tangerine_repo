#include "mqtt_task.h"
#include <ArduinoJson.h>

// MQTT broker IP — PC's ethernet adapter on the direct-link network
static IPAddress mqttBrokerIP(10, 10, 10, 10); 
//^for DAQ system (10,10,10,10)
//^may have to change when testing with Docker setup

// PubSubClient uses an EthernetClient under the hood
static EthernetClient ethClient;
static PubSubClient   mqtt(ethClient);

QueueHandle_t     xmqtt_cmd_queue    = NULL;
QueueHandle_t     xmqtt_status_queue = NULL;
SemaphoreHandle_t xmqtt_mutex        = NULL;

volatile uint8_t g_breakwire_publish_byte = 0;

TaskHandle_t vmqtt_task_handle              = NULL;
TaskHandle_t vmqtt_cmd_processor_task_handle = NULL;


/*
Queues a response for MQTT_RESPONSE_TOPIC: {"id": <id>, "status": "Ok"/"Error", "message": "..."}
error_msg == NULL means the command succeeded (status "Ok", empty message).
id_valid == false means we never got an id out of the command, so "id" is sent as null.
*/
static void sendResponse(long id, bool id_valid, const char* error_msg) {
    JsonDocument response;

    if (id_valid) {
        response["id"] = id;
    } else {
        response["id"] = nullptr;
    }
    response["status"]  = (error_msg == NULL) ? "Ok" : "Error";
    response["message"] = (error_msg == NULL) ? ""   : error_msg;

    char text[MQTT_STATUS_MSG_MAX_LEN];
    serializeJson(response, text, sizeof(text));
    publishStatus(MQTT_RESPONSE_TOPIC, text);
}


/*
MQTT callback — called by PubSubClient inside mqtt.loop() when a message arrives on a subscribed topic.
Expects JSON like {"command": 4, "id": 1}, where "command" is the gui_msg_t number.
Drops the command + id into xmqtt_cmd_queue for vmqtt_cmd_processor_task.
Anything that can't be turned into a command gets an "Error" response straight away.
Command numbers the board doesn't know are caught by the default case in the command processor.

Note: this runs inside vMqttTask (not a true ISR), so xQueueSend is used instead of xQueueSendFromISR.
*/
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err) {
        Serial.printf("[MQTT] Bad JSON (%s)\n", err.c_str());
        sendResponse(0, false, "invalid json");
        return;
    }

    // The id has to be read first so every later error can echo it back
    if (!doc["id"].is<long>()) {
        Serial.printf("[MQTT] Command has no valid id\n");
        sendResponse(0, false, "missing or invalid id");
        return;
    }
    long id = doc["id"].as<long>();

    // is<uint8_t>() is only true for a whole number from 0 to 255
    if (!doc["command"].is<uint8_t>()) {
        Serial.printf("[MQTT] Command id %ld has no valid command number\n", id);
        sendResponse(id, true, "missing or invalid command");
        return;
    }

    mqtt_cmd_t item;
    item.cmd = doc["command"].as<uint8_t>();
    item.id  = id;

    // Non-blocking send — if queue is full the command is dropped and the GUI is told
    if (xQueueSend(xmqtt_cmd_queue, &item, 0) != pdTRUE) {
        Serial.printf("[MQTT] Command queue full, dropped id %ld\n", id);
        sendResponse(id, true, "command queue full");
    }
}


/*
MQTT task — handles connection, reconnection, and calls mqtt.loop() every 10ms.
The server and callback are set here instead of in setup() so this task is fully self-contained.
(mirrors the Ethernet reconnect pattern from oldcode.cpp)
*/
void vmqtt_task(void *pvParameters) {
    mqtt.setServer(mqttBrokerIP, MQTT_PORT);
    mqtt.setCallback(mqttCallback);

    for (;;) {

        //Checks if mqtt is still connected, if not it attempts to reconnect
        //If it is able to connect we subscribe to the topic again
        //Otherwise we try again in 2 seconds
        if (!mqtt.connected()) {
            Serial.printf("[MQTT] Disconnected, attempting reconnect...\n");

            if (mqtt.connect(MQTT_CLIENT_ID)) {
                Serial.printf("[MQTT] Connected\n");
                mqtt.subscribe(MQTT_CMD_TOPIC);

            } else {
                Serial.printf("[MQTT] Failed, rc=%d, retrying in 2s\n", mqtt.state());
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue; //So we dont go into the rest of the task
            }
        }

        // Take mutex before using the mqtt object. Nothing else takes it any more now that
        // publishStatus() goes through a queue, but it stays as a guard for future code.
        if (xSemaphoreTake(xmqtt_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            mqtt.loop();

            // Publish breakwire status if breakwire_task flagged an update
            uint8_t bw = g_breakwire_publish_byte;
            if (bw != 0) {
                g_breakwire_publish_byte = 0;
                mqtt.publish(MQTT_BREAKWIRE_TOPIC, &bw, 1);
            }

            // Publish any status messages queued by publishStatus().
            // Peek first and only remove the message once it has actually been sent,
            // so a message isn't lost if the connection drops mid-way.
            mqtt_status_msg_t status_msg;
            while (xQueuePeek(xmqtt_status_queue, &status_msg, 0) == pdTRUE) {
                if (!mqtt.publish(status_msg.topic, status_msg.text)) {
                    Serial.printf("[MQTT] Status publish failed, will retry: %s\n", status_msg.text);
                    break; // leave it in the queue, try again next loop
                }
                xQueueReceive(xmqtt_status_queue, &status_msg, 0); // sent — remove it
            }

            xSemaphoreGive(xmqtt_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); //runs every 10ms

    }//for

}//task


/*
Queues a message for vmqtt_task to publish on the given topic.
Safe to call from any task (command processor, ignition task, etc.) because it only
copies the string into xmqtt_status_queue — vmqtt_task is the only task that touches
the mqtt object. Same idea as publishBreakwire() below.

Never blocks. If the queue is full the message is dropped and logged to Serial.
*/
void publishStatus(const char* topic, const char* msg) {
    if (xmqtt_status_queue == NULL) return; // queue not created yet

    mqtt_status_msg_t status_msg;
    status_msg.topic = topic;
    strncpy(status_msg.text, msg, MQTT_STATUS_MSG_MAX_LEN - 1);
    status_msg.text[MQTT_STATUS_MSG_MAX_LEN - 1] = '\0'; // strncpy doesn't terminate if msg is too long

    if (xQueueSend(xmqtt_status_queue, &status_msg, 0) != pdTRUE) {
        Serial.printf("[MQTT] Status queue full, dropped: %s\n", status_msg.text);
    }
}


/*
Publishes plain-text breakwire state to MQTT_BREAKWIRE_TOPIC ("broken" or "connected").
DAQ server (server/mqtt.py) subscribes to this topic and updates the controls page.
Separate from publishStatus() because the DAQ server reads a different topic for breakwire.
*/
void publishBreakwire(const char* status) {
    // Just set the flag — vmqtt_task publishes it on its next loop iteration.
    // This avoids calling mqtt.publish() from the breakwire task, which caused
    // mutex contention with mqtt.loop() and intermittent disconnects.
    g_breakwire_publish_byte = (strcmp(status, "connected") == 0) ? 0x10 : 0x11;
}


/*
Command processor task — reads gui_msg_t commands from xCommandQueue and routes them.
This is the equivalent of vprocess_can_rx_msg + handler functions from OPC main.c,
all collapsed into one switch case since we no longer need CAN class/component routing.
*/
void vmqtt_cmd_processor_task(void *pvParameters) {
    mqtt_cmd_t item;

    for (;;) {

        // Block until a command arrives in the queue (portMAX_DELAY = wait forever)
        if (xQueueReceive(xmqtt_cmd_queue, &item, portMAX_DELAY) == pdTRUE) {

            uint8_t cmd = item.cmd;

            Serial.printf("[CMD] Received cmd: 0x%02X (id %ld)\n", cmd, item.id);

            EventBits_t safetyFlags = xEventGroupGetBits(xsystem_safety_flags_handle);

            // Error reported back to the GUI once the command has been handled.
            // Stays NULL (= "Ok") unless a case below rejects the command.
            const char* error_msg = NULL;

            switch ((gui_msg_t)cmd) {

                // ---- SYSTEM FLAGS -------------------------------------------------------

                case GUI_MSG_ARM_IGNITER_SYSTEM:
                    xEventGroupSetBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED);
                    Serial.printf("[CMD] Igniter system ARMED\n");

                    // If breakwire is intact and LED is blinking, go solid to indicate armed
                    if ((brkwire_state == BREAKWIRE_CONNECTED) && (led2_state == LED2_BLINK_MEDIUM)) {
                        xTaskNotify(xtick_led2_handle, (uint32_t)LED2_ON, eSetValueWithOverwrite);
                    }
                break;

                case GUI_MSG_DISARM_IGNITER_SYSTEM:
                    xEventGroupClearBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED);
                    Serial.printf("[CMD] Igniter system DISARMED\n");

                    // Go back to blinking if breakwire is still intact
                    if ((brkwire_state == BREAKWIRE_CONNECTED) && (led2_state == LED2_ON)) {
                        xTaskNotify(xtick_led2_handle, (uint32_t)LED2_BLINK_MEDIUM, eSetValueWithOverwrite);
                    }
                break;

                case GUI_MSG_ARM_AUTO_IGNITION:
                    xEventGroupSetBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_AUTO_SEQUENCE_ARMED);
                    Serial.printf("[CMD] Auto ignition sequence ARMED\n");

                    // Wake the ignition task so it starts watching the breakwire
                    xTaskNotify(xtangerine_auto_ignition_task_handle, 0x01, eSetValueWithOverwrite);
                break;

                case GUI_MSG_DISARM_AUTO_IGNITION:
                    xEventGroupClearBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_AUTO_SEQUENCE_ARMED);
                    Serial.printf("[CMD] Auto ignition sequence DISARMED\n");
                break;

                case GUI_MSG_ARM_SERVOS:
                    xEventGroupSetBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_SERVOS_ARMED);
                    Serial.printf("[CMD] Servos ARMED\n");
                break;

                case GUI_MSG_DISARM_SERVOS:
                    xEventGroupClearBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_SERVOS_ARMED);
                    Serial.printf("[CMD] Servos DISARMED\n");
                break;

                // ---- SERVO ACTUATION ----------------------------------------------------
                // All servo commands require SERVOS_ARMED

                case GUI_MSG_OPEN_SERVO_1:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        servo1.writeMicroseconds(FILL_VALVE_OPEN_US);
                        Serial.printf("[CMD] Servo 1 (fill valve) OPEN\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 1 open — servos not armed\n");
                        error_msg = "servos not armed";
                    }
                break;

                case GUI_MSG_CLOSE_SERVO_1:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        servo1.writeMicroseconds(FILL_VALVE_CLOSE_US);
                        Serial.printf("[CMD] Servo 1 (fill valve) CLOSE\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 1 close — servos not armed\n");
                        error_msg = "servos not armed";
                    }
                break;

                case GUI_MSG_OPEN_SERVO_2:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        servo2.writeMicroseconds(RUN_VALVE_OPEN_US);
                        Serial.printf("[CMD] Servo 2 (run valve) OPEN\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 2 open — servos not armed\n");
                        error_msg = "servos not armed";
                    }
                break;

                case GUI_MSG_CLOSE_SERVO_2:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        servo2.writeMicroseconds(RUN_VALVE_CLOSE_US);
                        Serial.printf("[CMD] Servo 2 (run valve) CLOSE\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 2 close — servos not armed\n");
                        error_msg = "servos not armed";
                    }
                break;

                case GUI_MSG_OPEN_SERVO_3:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        servo3.writeMicroseconds(RUN_VALVE_OPEN_US);
                        Serial.printf("[CMD] Servo 3 (run valve) OPEN\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 3 open — servos not armed\n");
                        error_msg = "servos not armed";
                    }
                break;

                case GUI_MSG_CLOSE_SERVO_3:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        servo3.writeMicroseconds(RUN_VALVE_CLOSE_US);
                        Serial.printf("[CMD] Servo 3 (run valve) CLOSE\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 3 close — servos not armed\n");
                        error_msg = "servos not armed";
                    }
                break;

                // ---- IGNITER ------------------------------------------------------------

                case GUI_MSG_IGNITER_FIRE:
                    if (safetyFlags & SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED) {
                        FIRE_TANGERINE_IGNITER();
                        Serial.printf("[CMD] IGNITER FIRED\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Igniter fire — igniter system not armed\n");
                        error_msg = "igniter system not armed";
                    }
                break;

                case GUI_MSG_IGNITER_OFF:
                    OFF_TANGERINE_IGNITER();
                    Serial.printf("[CMD] Igniter OFF\n");
                break;

                // ---- LED PING -----------------------------------------------------------

                case GUI_MSG_LED_OFF:
                    xTaskNotify(xtick_led2_handle, (uint32_t)LED2_OFF, eSetValueWithOverwrite);
                break;

                case GUI_MSG_LED_MEDIUM:
                    xTaskNotify(xtick_led2_handle, (uint32_t)LED2_BLINK_BLUE, eSetValueWithOverwrite);
                break;

                default:
                    Serial.printf("[CMD] Unknown command: 0x%02X\n", cmd);
                    error_msg = "unknown command";
                break;

            }//switch

            // Tell the GUI what happened to the command, echoing its id
            sendResponse(item.id, true, error_msg);

        }//if xQueueReceive

    }//for

}//task