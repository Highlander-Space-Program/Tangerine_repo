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

#define SERVO_FULL_CW_DC      SERVO_US_TO_DC(800)   // 2621 — full clockwise (empirical, 600 too low)
#define SERVO_NEUTRAL_DC      SERVO_US_TO_DC(1500)  // 4915 — neutral / 135°
#define SERVO_FULL_CCW_DC     SERVO_US_TO_DC(2400)  // 7864 — full counter-clockwise

// TODO: verify open/close direction matches physical valve orientation
#define RUN_VALVE_OPEN_DC     SERVO_FULL_CCW_DC
#define RUN_VALVE_CLOSE_DC    SERVO_FULL_CW_DC


void vtangerine_auto_ignition_task(void *pvParameters);

// Called by the ignition task internally AND by the command processor for manual GUI control
void OPEN_TANGERINE_RUN_VALVES(void);
void CLOSE_TANGERINE_RUN_VALVES(void);

void FIRE_TANGERINE_IGNITER(void);
void OFF_TANGERINE_IGNITER(void);


extern TaskHandle_t xtangerine_auto_ignition_task_handle;
