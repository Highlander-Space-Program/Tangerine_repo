#pragma once

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <event_groups.h>

#include "system_safety_flags.h"
#include "breakwire_task.h"

// Run valve servo pins (servos 2 & 3, opened simultaneously as run valves)
#define SERVO_2_PIN 0
#define SERVO_3_PIN 4

// Igniter fire pin
#define IGNITER_FIRE_PIN 13

// TODO: Set these to the actual calibrated PWM duty cycles for your servos
// analogWriteResolution(8) + analogWriteFreq(1000) -> 0-255 maps to 0-1ms period
#define RUN_VALVE_OPEN_DC  200  // duty cycle for fully open
#define RUN_VALVE_CLOSE_DC 50   // duty cycle for fully closed


void vtangerine_auto_ignition_task(void *pvParameters);

// Called by the ignition task internally AND by the command processor for manual GUI control
void OPEN_TANGERINE_RUN_VALVES(void);
void CLOSE_TANGERINE_RUN_VALVES(void);

void FIRE_TANGERINE_IGNITER(void);
void OFF_TANGERINE_IGNITER(void);


extern TaskHandle_t xtangerine_auto_ignition_task_handle;
