#include "ignition_task.h"

TaskHandle_t xtangerine_auto_ignition_task_handle = NULL;


// Opens servos 2 & 3 simultaneously (run valves)
// NOTE: RUN_VALVE_OPEN_DC needs to be calibrated to the actual servo open angle
void OPEN_TANGERINE_RUN_VALVES(void) {
    analogWrite(SERVO_2_PIN, RUN_VALVE_OPEN_DC);
    analogWrite(SERVO_3_PIN, RUN_VALVE_OPEN_DC);
}

// Closes servos 2 & 3 simultaneously (run valves)
void CLOSE_TANGERINE_RUN_VALVES(void) {
    analogWrite(SERVO_2_PIN, RUN_VALVE_CLOSE_DC);
    analogWrite(SERVO_3_PIN, RUN_VALVE_CLOSE_DC);
}

// Sets IGNITER_FIRE_PIN HIGH — fires the igniter e-match
void FIRE_TANGERINE_IGNITER(void) {
    digitalWrite(IGNITER_FIRE_PIN, HIGH);
}

// Sets IGNITER_FIRE_PIN LOW — turns off igniter e-match
// replaces: HAL_GPIO_WritePin(PYRO_SW2_GPIO_Port, PYRO_SW2_Pin, GPIO_PIN_RESET)
void OFF_TANGERINE_IGNITER(void) {
    digitalWrite(IGNITER_FIRE_PIN, LOW);
}





void vtangerine_auto_ignition_task(void *pvParameters) {
    // Make sure run valves start closed
    CLOSE_TANGERINE_RUN_VALVES();

    uint32_t cmd_from_notif;

    for (;;) {

        /*
        1. DORMANT STATE
        Task sleeps here until auto ignition sequence is ARMED. When this task wakes it will then watch the break-wire.
        Once the break-wire loses continuity this task will fire the pyro-valve and then go back to sleep, forever this time
        */

        /*
        0 means that it won't clear any of the notification bits before checking whether or not there's a pending notification
        0xFFFFFFFF means that ALL notification bits will be reset/cleared after checking if there's a pending notif
        cmd_from_notif copies the value of the notification that woke the task
        Waits for portMAX_DELAY (forever) until a notification arrives
        */
        xTaskNotifyWait(0, 0xFFFFFFFF, &cmd_from_notif, portMAX_DELAY);

        Serial.printf("[IGNITION] Auto ignition sequence ARMED — watching breakwire\n");

        /*
        2. WATCHING BREAKWIRE
        Here is where the task then waits for the BREAKWIRE_IS_BROKEN bit of the breakwire_event_group to go high.
        Once it does then we proceed to open the run valves (servos 2 & 3).
        */

        /*
        xEventGroupWaitBits will block the task until certain conditions are met.

        xbreakwire_event_group_handle is obviously the event group we're looking to check the bits of.

        BREAKWIRE_IS_BROKEN_MASK masks for a specific bit, we only configure for one bit here, but it can be configured for more bit positions as well.

        The first pdFALSE is for the ClearOnExit parameter which can clear (set to 0) the bits we masked for (breakwire s broken bit), we DO NOT want to do that here

        The second pdTRUE is for the WaitForAllBits parameter, this tells the function whether we want to wait for ALL the bits we masked for to be set
        or if we simply want to wait for ANY ONE of the bits to be set before we wake the task. Here we choose pdTRUE although we only mask for one bit to
        communicate that this is a strict requirement.

        Then we say how long to wait for the bit to be set, here we just wait forever
        */
        EventBits_t breakwireStatus = xEventGroupWaitBits(xbreakwire_event_group_handle,
                                                           BREAKWIRE_IS_BROKEN_MASK,
                                                           pdFALSE,
                                                           pdTRUE,
                                                           portMAX_DELAY);

        Serial.printf("[IGNITION] Breakwire broken — running final safety checks\n");

        /*
        3. FINAL CHECKS AND OPENING RUN VALVES
        Now that the breakwire is broken we'll pull the latest values from system_safety_flags event group, we verify that the
        ignition system and auto ignition flags are still set & that the breakwire is broken in breakwireStatus.

        We double check the flags to ensure we haven't aborted (TODO: implement an abort flag) in the time we waited
        for the breakwire to break.

        After checking allat we will call a function that sets off the pyro valves. It is a function so that we can also manually
		actuate the run valves
        */
        EventBits_t safetyFlags = xEventGroupGetBits(xsystem_safety_flags_handle);

        if ( (breakwireStatus & BREAKWIRE_IS_BROKEN_MASK) &&
             (safetyFlags & (SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED | SAFETY_FLAGS_MASK_AUTO_SEQUENCE_ARMED)) ) {

            // Both flags still set and breakwire confirmed broken — open the run valves
            Serial.printf("[IGNITION] Checks passed — OPENING RUN VALVES\n");
            OPEN_TANGERINE_RUN_VALVES();

        } else {
            // One or more flags were cleared (aborted) while waiting — safe shutdown
            Serial.printf("[IGNITION] Safety check FAILED (flags cleared?) — aborting, closing run valves\n");
            CLOSE_TANGERINE_RUN_VALVES();
            xEventGroupClearBits(xsystem_safety_flags_handle, SAFETY_FLAGS_MASK_AUTO_SEQUENCE_ARMED);
        }

    }//for

}//task


