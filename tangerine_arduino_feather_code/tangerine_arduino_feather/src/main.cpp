#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Adafruit_NeoPixel.h>
#include <FreeRTOS.h>
#include <task.h>
#include <event_groups.h>

#include "led_task.h"
#include "breakwire_task.h"
#include "system_safety_flags.h"
#include "ignition_task.h"
#include "mqtt_task.h"

// NeoPixel
#define NEOPIXEL_PIN       21
#define NEOPIXEL_POWER_PIN 20
#define NUM_PIXELS          1
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Event groups — defined here, extern'd in task headers
EventGroupHandle_t xsystem_safety_flags_handle   = NULL;


void setup() {
    Serial.begin(115200);
    delay(1500);

    // NeoPixel power rail
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);
    delay(10);
    pixel.begin();
    pixel.setBrightness(20);

    // Blink green 3 times — setup() is alive
    for (int i = 0; i < 3; i++) {
        pixel.setPixelColor(0, pixel.Color(0, 150, 0));
        pixel.show();
        delay(200);
        pixel.setPixelColor(0, 0);
        pixel.show();
        delay(200);
    }

    // Servo PWM — 50Hz standard servo frequency, 16-bit resolution
    analogWriteResolution(16);
    analogWriteFreq(50);

    // Run valve pins — start LOW (safe/closed)
    pinMode(SERVO_2_PIN, OUTPUT); digitalWrite(SERVO_2_PIN, LOW);
    pinMode(SERVO_3_PIN, OUTPUT); digitalWrite(SERVO_3_PIN, LOW);

    // Igniter fire pin — starts LOW (safe)
    pinMode(IGNITER_FIRE_PIN, OUTPUT);
    digitalWrite(IGNITER_FIRE_PIN, LOW);

    // Deselect the on-board MCP2515 CAN controller — shares SPI bus with W5500 FeatherWing.
    // Floating CS corrupts SPI transactions to the W5500.
    // adafruit_feather_can board definition maps SPI to GPIO 14/15/8 (SPI1 pins on the Feather header).
    pinMode(9,  OUTPUT); digitalWrite(9,  HIGH); // CAN CS — deselect
    pinMode(16, OUTPUT); digitalWrite(16, HIGH); // CAN STANDBY

    Serial.println("[MAIN] SPI begin...");
    SPI.begin();
    Serial.println("[MAIN] Ethernet init...");
    Ethernet.init(SPI_CS_PIN);

    static byte      mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
    static IPAddress ip (192, 168, 100, 50);
    static IPAddress gw (192, 168, 100,  1);
    static IPAddress sn (255, 255, 255,  0);
    Ethernet.begin(mac, ip, gw, gw, sn);

    auto hw = Ethernet.hardwareStatus();
    if (hw == EthernetNoHardware) Serial.println("[MAIN] W5500: NOT FOUND (SPI failure)");
    else if (hw == EthernetW5500) Serial.println("[MAIN] W5500: found OK");
    else                          Serial.printf("[MAIN] W5500: hardware code %d\n", hw);

    Serial.println("[MAIN] Waiting for link...");
    for (int i = 0; i < 10; i++) {
        delay(500);
        if (Ethernet.linkStatus() == LinkON) {
            Serial.println("[MAIN] Link: UP");
            break;
        }
        Serial.printf("[MAIN] Link: DOWN (%d/10)\n", i + 1);
    }

    Serial.printf("[MAIN] IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    // Breakwire circuit
    pinMode(BRKWIRE_2_GND,    OUTPUT);
    digitalWrite(BRKWIRE_2_GND, LOW);
    pinMode(BRKWIRE_2_DETECT, INPUT_PULLUP);

    // Create event groups, queue, and mutex before tasks that use them
    xsystem_safety_flags_handle   = xEventGroupCreate();
    xbreakwire_event_group_handle = xEventGroupCreate();
    xmqtt_cmd_queue               = xQueueCreate(10, sizeof(uint8_t));
    xmqtt_mutex                   = xSemaphoreCreateMutex();

    // Create tasks
    xTaskCreate(vtick_led2,                    "led2",            512, &pixel, 2, &xtick_led2_handle);
    xTaskCreate(vcheck_breakwire_task,         "breakwire",       512, NULL,   3, &xcheck_breakwire_task_handle);
    xTaskCreate(vtangerine_auto_ignition_task, "ignition",        512, NULL,   3, &xtangerine_auto_ignition_task_handle);
    xTaskCreate(vmqtt_task,                    "mqtt",            512, NULL,   2, &vmqtt_task_handle);
    xTaskCreate(vmqtt_cmd_processor_task,      "cmd_processing",  512, NULL,   2, &vmqtt_cmd_processor_task_handle);
}


void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
