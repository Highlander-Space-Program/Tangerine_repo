#include "breakwire_task.h"
#include "mqtt_task.h"

volatile breakwire_states_t brkwire_state = BREAKWIRE_INIT;
EventGroupHandle_t xbreakwire_event_group_handle = NULL;

TaskHandle_t xcheck_breakwire_task_handle = NULL;


void vcheck_breakwire_task(void *pvParameters) {
    TickType_t first_break_detection_tick  = 0;
    TickType_t current_tick             = 0;


    for (;;) {
        // Read the breakwire detect pin
        // LOW  = wire is intact (grounded through wire) = CONNECTED
        // HIGH = wire is broken (pin floating high)     = DISCONNECTED
        int currentRead = digitalRead(BRKWIRE_2_DETECT);
        current_tick     = xTaskGetTickCount();

        //Check if igniter system is armed
        EventBits_t safetyFlags        = xEventGroupGetBits(xsystem_safety_flags_handle);
        BaseType_t  igniterSystemArmed = (safetyFlags & SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED);

        led2_state_t led_cmd;


        switch (brkwire_state) { //SWITCH CASE FOR TRANSITIONS

            case BREAKWIRE_INIT:
                if (currentRead == LOW) { // LOW = CONNECTED
                    brkwire_state = BREAKWIRE_CONNECTED;

                    first_break_detection_tick  = 0; // reset de-bounce counter

                    // if igniter armed solid on, if not = medium blink
                    led_cmd = (igniterSystemArmed) ? LED2_ON : LED2_BLINK_MEDIUM; //set LED command
                    xTaskNotify(xtick_led2_handle, (uint32_t)led_cmd, eSetValueWithOverwrite); // update led task with newest command

                    Serial.printf("[BRKWIRE] INIT -> CONNECTED (igniter %s)\n", igniterSystemArmed ? "ARMED" : "unarmed");

                } else { // HIGH = DISCONNECTED
                    first_break_detection_tick  = current_tick; // start timer (in ticks)
                    brkwire_state = BREAKWIRE_VALIDATE_DISCONNECTION;

                    Serial.printf("[BRKWIRE] INIT -> VALIDATE_DISCONNECTION (pin HIGH at boot)\n");
                }
            break;


            case BREAKWIRE_CONNECTED:
                if (currentRead == HIGH) { // HIGH = DISCONNECTED
                    first_break_detection_tick  = current_tick; // start timer (in ticks)
                    brkwire_state = BREAKWIRE_VALIDATE_DISCONNECTION;

                    Serial.printf("[BRKWIRE] CONNECTED -> VALIDATE_DISCONNECTION (debouncing...)\n");

                } else { // else --> LOW = still CONNECTED
                    first_break_detection_tick  = 0;
                }
            break;


            case BREAKWIRE_VALIDATE_DISCONNECTION:

                if (currentRead == LOW) { // LOW = CONNECTED (bounce — wire still intact)
                    brkwire_state = BREAKWIRE_CONNECTED;
                    first_break_detection_tick  = 0;

                    // if igniter armed = LED solid on, if not = LED medium blink
                    led_cmd = (igniterSystemArmed) ? LED2_ON : LED2_BLINK_MEDIUM;
                    xTaskNotify(xtick_led2_handle, (uint32_t)led_cmd, eSetValueWithOverwrite); // update led task with newest command

                    Serial.printf("[BRKWIRE] VALIDATE_DISCONNECTION -> CONNECTED (bounce, wire still intact)\n");

                } else if ((current_tick - first_break_detection_tick ) >= BROKEN_BRKWIRE_DEBOUNCE_TICKS) { // HIGH long enough = DISCONNECTED CONFIRMED

                    brkwire_state = BREAKWIRE_CONFIRMED_DISCONNECTED;

                    // Set event bit: BREAKWIRE IS BROKEN
                    xEventGroupSetBits(xbreakwire_event_group_handle, BREAKWIRE_IS_BROKEN_MASK);

                    publishBreakwire("broken"); // replaces: GIC_send_ui_update(BREAKWIRE_BROKEN_MSG, ...)

                    // Notify LED task: breakwire is broken → LED off
                    led_cmd = LED2_OFF;
                    xTaskNotify(xtick_led2_handle, (uint32_t)led_cmd, eSetValueWithOverwrite); // update led task with newest command

                    Serial.printf("[BRKWIRE] VALIDATE_DISCONNECTION -> CONFIRMED_DISCONNECTED (breakwire BROKEN)\n");

                }
            break;


            case BREAKWIRE_CONFIRMED_DISCONNECTED:
                if (currentRead == LOW) { // LOW = wire reconnected
                    brkwire_state = BREAKWIRE_CONNECTED;
                    first_break_detection_tick  = 0;

                    // Clear event bit: BREAKWIRE IS BROKEN
                    xEventGroupClearBits(xbreakwire_event_group_handle, BREAKWIRE_IS_BROKEN_MASK);

                    publishBreakwire("connected"); // replaces: GIC_send_ui_update(BREAKWIRE_CONNECTED_MSG, ...)

                    led_cmd = (igniterSystemArmed) ? LED2_ON : LED2_BLINK_MEDIUM;
                    xTaskNotify(xtick_led2_handle, (uint32_t)led_cmd, eSetValueWithOverwrite); // update led task with newest command

                    Serial.printf("[BRKWIRE] CONFIRMED_DISCONNECTED -> CONNECTED (wire restored, igniter %s)\n", igniterSystemArmed ? "ARMED" : "unarmed");

                } else { // HIGH = still disconnected
                    // nothing
                }
            break;

        } //SWITCH CASE FOR TRANSITIONS

        // No real state actions so we can skip the second switch statement

        vTaskDelay(pdMS_TO_TICKS(CHECK_BREAKWIRE_TASK_DELAY_MS));
    
    }//for

} //task
