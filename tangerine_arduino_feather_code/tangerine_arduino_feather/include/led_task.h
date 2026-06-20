#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <FreeRTOS.h>
#include <task.h>

typedef enum {
    LED2_ON,
    LED2_OFF,
    LED2_BLINK_SLOW,
    LED2_BLINK_MEDIUM,
    LED2_BLINK_FAST,
    LED2_BLINK_BLUE   // used for ping response
} led2_state_t;

//No CAN cmd to LED state LUT needed anymore
extern volatile led2_state_t led2_state; //state of the LED


extern TaskHandle_t xtick_led2_handle; //Handle for the task
void vtick_led2(void *pvParameters); //It'll pass in a pointer to th neopixel object now
