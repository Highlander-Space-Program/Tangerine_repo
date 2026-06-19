#pragma once

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <event_groups.h>

#include "led_task.h"
#include "system_safety_flags.h"

// Breakwire pin definitions
// BRKWR_3 -> D12 GND    (driven LOW as circuit reference ground)
// BRKWR_4 -> D11 DETECT (pulled HIGH; wire intact pulls it LOW, wire broken = HIGH)
#define BRKWIRE_2_GND               12
#define BRKWIRE_2_DETECT            11

#define BROKEN_BRKWIRE_DEBOUNCE_MS          20
#define BROKEN_BRKWIRE_DEBOUNCE_TICKS       pdMS_TO_TICKS(BROKEN_BRKWIRE_DEBOUNCE_MS)
#define CHECK_BREAKWIRE_TASK_DELAY_MS       5   // check the breakwire 200 times per second (5ms)

#define BREAKWIRE_IS_BROKEN_SHIFT           0
#define BREAKWIRE_IS_BROKEN_MASK            (1 << BREAKWIRE_IS_BROKEN_SHIFT) // 0x00000001 = 0b0001


typedef enum {
    BREAKWIRE_INIT,
    BREAKWIRE_CONNECTED,
    BREAKWIRE_VALIDATE_DISCONNECTION,
    BREAKWIRE_CONFIRMED_DISCONNECTED
} breakwire_states_t;

extern volatile breakwire_states_t brkwire_state;
extern EventGroupHandle_t xbreakwire_event_group_handle;

// Defined in main.cpp, extern'd here so breakwire task can check igniter armed flag
extern EventGroupHandle_t xsystem_safety_flags_handle;

extern TaskHandle_t xcheck_breakwire_task_handle;
void vcheck_breakwire_task(void *pvParameters);
