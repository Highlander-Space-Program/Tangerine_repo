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

// Servo PWM — 50Hz (20ms period), 16-bit resolution (0-65535)
// DS3235 practical range per team: 600-2400μs (spec says 500-2500 but slightly off)
// duty = pulse_us * 65536 / 20000
#define SERVO_US_TO_DC(us)    ((uint32_t)(us) * 65536 / 20000)

#define SERVO_NEUTRAL_DC      SERVO_US_TO_DC(1500)  // 4915 — neutral / 135°

// Pulled in from extremes to avoid stalling against mechanical stops (causes overheating).
// Calibrate these: increase OPEN_US toward 2400 / decrease CLOSE_US toward 600
// until valve just reaches fully open/closed, then back off ~50-100us.
#define RUN_VALVE_OPEN_US     2000
#define RUN_VALVE_CLOSE_US    1000

#define RUN_VALVE_OPEN_DC     SERVO_US_TO_DC(RUN_VALVE_OPEN_US)
#define RUN_VALVE_CLOSE_DC    SERVO_US_TO_DC(RUN_VALVE_CLOSE_US)


void vtangerine_auto_ignition_task(void *pvParameters);

// Called by the ignition task internally AND by the command processor for manual GUI control
void OPEN_TANGERINE_RUN_VALVES(void);
void CLOSE_TANGERINE_RUN_VALVES(void);

void FIRE_TANGERINE_IGNITER(void);
void OFF_TANGERINE_IGNITER(void);


extern TaskHandle_t xtangerine_auto_ignition_task_handle;
