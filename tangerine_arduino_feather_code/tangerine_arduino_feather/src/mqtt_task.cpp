#include "mqtt_task.h"

// MQTT broker IP — PC's ethernet adapter on the direct-link network
static IPAddress mqttBrokerIP(192, 168, 100, 1);

// PubSubClient uses an EthernetClient under the hood
static EthernetClient ethClient;
static PubSubClient   mqtt(ethClient);

QueueHandle_t     xmqtt_cmd_queue = NULL;
SemaphoreHandle_t xmqtt_mutex     = NULL;

volatile uint8_t g_breakwire_publish_byte = 0;

TaskHandle_t vmqtt_task_handle              = NULL;
TaskHandle_t vmqtt_cmd_processor_task_handle = NULL;


/*
MQTT callback — called by PubSubClient inside mqtt.loop() when a message arrives on a subscribed topic.
Grabs the first byte of the payload (which is the gui_msg_t command) and drops it into xCommandQueue.

Note: this runs inside vMqttTask (not a true ISR), so xQueueSend is used instead of xQueueSendFromISR.
The ISR variant would also work here but xQueueSend is the correct choice for task context.
*/
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (length < 1) return;

    uint8_t cmd = payload[0];

    // Non-blocking send — if queue is full the command is dropped
    // (mirrors xQueueSendFromISR pattern from OPC CAN ISR, adapted for task context)
    if (xQueueSend(xmqtt_cmd_queue, &cmd, 0) != pdTRUE) {
        Serial.printf("[MQTT] Command queue full, dropped cmd: 0x%02X\n", cmd);
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

        // Take mutex before calling mqtt.loop() so publishStatus() from other tasks
        // can't overlap with it (we're protecting the mqtt object)
        if (xSemaphoreTake(xmqtt_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            mqtt.loop();

            // Publish breakwire status if breakwire_task flagged an update
            uint8_t bw = g_breakwire_publish_byte;
            if (bw != 0) {
                g_breakwire_publish_byte = 0;
                mqtt.publish(MQTT_BREAKWIRE_TOPIC, &bw, 1);
            }

            xSemaphoreGive(xmqtt_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); //runs every 10ms

    }//for

}//task


/*
Publishes a status string to MQTT_STATUS_TOPIC.
Thread-safe — takes xMqttMutex before calling mqtt.publish() so this can be called
from any task (breakwire_task, command processor, etc.).
*/
void publishStatus(const char* msg) {
    if (!mqtt.connected()) return;

    if (xSemaphoreTake(xmqtt_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        mqtt.publish(MQTT_STATUS_TOPIC, msg);
        xSemaphoreGive(xmqtt_mutex);
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
    uint8_t cmd;

    for (;;) {

        // Block until a command arrives in the queue (portMAX_DELAY = wait forever)
        if (xQueueReceive(xmqtt_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {

            Serial.printf("[CMD] Received cmd: 0x%02X\n", cmd);

            EventBits_t safetyFlags = xEventGroupGetBits(xsystem_safety_flags_handle);

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
                        analogWrite(SERVO_1_PIN, FILL_VALVE_OPEN_DC);
                        Serial.printf("[CMD] Servo 1 (fill valve) OPEN\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 1 open — servos not armed\n");
                    }
                break;

                case GUI_MSG_CLOSE_SERVO_1:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        analogWrite(SERVO_1_PIN, FILL_VALVE_CLOSE_DC);
                        Serial.printf("[CMD] Servo 1 (fill valve) CLOSE\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 1 close — servos not armed\n");
                    }
                break;

                case GUI_MSG_OPEN_SERVO_2:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        analogWrite(SERVO_2_PIN, RUN_VALVE_OPEN_DC);
                        Serial.printf("[CMD] Servo 2 (run valve) OPEN\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 2 open — servos not armed\n");
                    }
                break;

                case GUI_MSG_CLOSE_SERVO_2:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        analogWrite(SERVO_2_PIN, RUN_VALVE_CLOSE_DC);
                        Serial.printf("[CMD] Servo 2 (run valve) CLOSE\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 2 close — servos not armed\n");
                    }
                break;

                case GUI_MSG_OPEN_SERVO_3:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        analogWrite(SERVO_3_PIN, RUN_VALVE_OPEN_DC);
                        Serial.printf("[CMD] Servo 3 (run valve) OPEN\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 3 open — servos not armed\n");
                    }
                break;

                case GUI_MSG_CLOSE_SERVO_3:
                    if (safetyFlags & SAFETY_FLAGS_MASK_SERVOS_ARMED) {
                        analogWrite(SERVO_3_PIN, RUN_VALVE_CLOSE_DC);
                        Serial.printf("[CMD] Servo 3 (run valve) CLOSE\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Servo 3 close — servos not armed\n");
                    }
                break;

                // ---- IGNITER ------------------------------------------------------------

                case GUI_MSG_IGNITER_FIRE:
                    if (safetyFlags & SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED) {
                        FIRE_TANGERINE_IGNITER();
                        Serial.printf("[CMD] IGNITER FIRED\n");
                    } else {
                        Serial.printf("[CMD] REJECTED: Igniter fire — igniter system not armed\n");
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
                break;

            }//switch

        }//if xQueueReceive

    }//for

}//task
