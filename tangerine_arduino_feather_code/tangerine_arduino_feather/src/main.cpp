//Includes
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <FreeRTOS.h>
#include <task.h>

// NeoPixel stuff
#define NEOPIXEL_PIN       21
#define NEOPIXEL_POWER_PIN 20
#define NUM_PIXELS          1
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Servo 1 (fill valve) pin define lives in mqtt_task.h
// Servo 2 & 3 (run valves) pin defines live in ignition_task.h

//freeRTOS tasks
#include <event_groups.h>
#include "led_task.h"
#include "breakwire_task.h"
#include "system_safety_flags.h"
#include "ignition_task.h"
#include "mqtt_task.h"


// Igniter fire pin define lives in ignition_task.h

// Event groups - safety flags & breakwire state
// (defined here, extern'd in breakwire_task.h so that task can read/write them)
EventGroupHandle_t xsystem_safety_flags_handle    = NULL;

// GUI message enum lives in mqtt_task.h


//Setup
void setup() {
    Serial.begin(115200);
    delay(1500); // give USB CDC time to connect before printing — avoids missing early output

    // NeoPixel power rail
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);
    delay(10);

    pixel.begin();
    pixel.setBrightness(20);

    // Blink green 3 times to confirm setup() is running
    for (int i = 0; i < 3; i++) {
        pixel.setPixelColor(0, pixel.Color(0, 150, 0));
        pixel.show();
        delay(200);
        pixel.setPixelColor(0, 0);
        pixel.show();
        delay(200);
    }

    // Servo PWM
    analogWriteResolution(8);
    analogWriteFreq(1000);

    // Igniter fire pin — starts LOW (safe)
    pinMode(IGNITER_FIRE_PIN, OUTPUT);
    digitalWrite(IGNITER_FIRE_PIN, LOW);

    // W5500 Ethernet SPI setup
    // SPI0 default pins (18/19/20) match the FeatherWing header — no pin remapping needed
    // SPI.setSCK/TX/RX not called: GPIO 14/15/8 are SPI1 pins and calling setSCK(14) on
    // the SPI0 object panics in the Earlephilhower core
    Serial.printf("[MAIN] SPI begin...\n");
    SPI.begin();
    Serial.printf("[MAIN] SPI done, Ethernet init...\n");
    Ethernet.init(SPI_CS_PIN);
    Serial.printf("[MAIN] Ethernet init done, Ethernet begin...\n");

    static IPAddress ip  (192, 168, 100, 50);   // board static IP
    static IPAddress gw  (192, 168, 100,  1);   // PC ethernet adapter (direct link)
    static IPAddress sn  (255, 255, 255,  0);
    static byte      mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
    Ethernet.begin(mac, ip, gw, gw, sn);
    Serial.printf("[MAIN] Ethernet IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    //Breakwire circuit stuff
    pinMode(BRKWIRE_2_GND, OUTPUT); 
    digitalWrite(BRKWIRE_2_GND, LOW); //Set GND pin LOW so the breakwire circuit has a reference ground

    pinMode(BRKWIRE_2_DETECT, INPUT_PULLUP); // DETECT pin pulled HIGH internally — when wire is intact it gets pulled to GND (reads LOW)
                                             // when wire breaks it floats HIGH

    // Create event groups and queues before tasks that use them
    xsystem_safety_flags_handle    = xEventGroupCreate();
    xbreakwire_event_group_handle  = xEventGroupCreate();
    xmqtt_cmd_queue                = xQueueCreate(10, sizeof(uint8_t));
    xmqtt_mutex                    = xSemaphoreCreateMutex();

    // Create tasks
    xTaskCreate(vtick_led2,                       "led2",      512,  &pixel, 2, &xtick_led2_handle);
    xTaskCreate(vcheck_breakwire_task,            "breakwire", 512,  NULL,   3, &xcheck_breakwire_task_handle);
    xTaskCreate(vtangerine_auto_ignition_task,    "ignition",  512,  NULL,   3, &xtangerine_auto_ignition_task_handle);
    xTaskCreate(vmqtt_task,                       "mqtt",      512,  NULL,   2, &vmqtt_task_handle);
    xTaskCreate(vmqtt_cmd_processor_task,         "cmd_proc",  512,  NULL,   2, &vmqtt_cmd_processor_task_handle);
}


//main loop
void loop() {
    // All work happens in FreeRTOS tasks.
    // loop() itself runs as a low-priority task in the Earlephilhower core;
    // keep it alive with a yield so it doesn't starve other tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
