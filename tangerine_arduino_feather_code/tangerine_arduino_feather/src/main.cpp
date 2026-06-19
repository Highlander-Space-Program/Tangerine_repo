#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Adafruit_NeoPixel.h>
#include <FreeRTOS.h>
#include <task.h>

// NeoPixel
#define NEOPIXEL_PIN       21
#define NEOPIXEL_POWER_PIN 20
#define NUM_PIXELS          1
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// W5500 CS pin (Feather SPI0 default: SCK=18, MOSI=19, MISO=20)
#define SPI_CS_PIN  10

// Heartbeat task — blinks blue every second and prints link status
void vHeartbeatTask(void *pvParameters) {
    Adafruit_NeoPixel *px = (Adafruit_NeoPixel *)pvParameters;
    for (;;) {
        px->setPixelColor(0, px->Color(0, 0, 80));
        px->show();
        vTaskDelay(pdMS_TO_TICKS(100));
        px->setPixelColor(0, 0);
        px->show();

        Serial.printf("[HEARTBEAT] Link: %s\n",
            Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    // NeoPixel power rail
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);
    delay(10);
    pixel.begin();
    pixel.setBrightness(20);

    // Blink green 2 times — setup() is alive
    for (int i = 0; i < 2; i++) {
        pixel.setPixelColor(0, pixel.Color(0, 150, 0));
        pixel.show();
        delay(200);
        pixel.setPixelColor(0, 0);
        pixel.show();
        delay(200);
    }

    // Ethernet init — SPI0 defaults (no remapping needed for FeatherWing header)
    Serial.println("[MAIN] SPI begin...");
    SPI.begin();
    Serial.println("[MAIN] Ethernet init...");
    Ethernet.init(SPI_CS_PIN);

    static byte      mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
    static IPAddress ip (192, 168, 100, 50);
    static IPAddress gw (192, 168, 100,  1);
    static IPAddress sn (255, 255, 255,  0);
    Ethernet.begin(mac, ip, gw, gw, sn);
    delay(500); // give W5500 time to negotiate link

    Serial.printf("[MAIN] IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[MAIN] Link: %s\n", Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");

    xTaskCreate(vHeartbeatTask, "heartbeat", 256, &pixel, 1, NULL);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
