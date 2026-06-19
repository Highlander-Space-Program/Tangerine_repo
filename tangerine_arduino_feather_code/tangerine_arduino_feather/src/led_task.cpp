#include "led_task.h"

//No more LUT

volatile led2_state_t led2_state = LED2_OFF; //State

TaskHandle_t xtick_led2_handle = NULL; //Handle

/*
Direct translation of vtick_led2() from gic_leds_OPC.c.
    HAL_GPIO_WritePin  -> pixel->setPixelColor() + pixel->show()
    HAL_GPIO_TogglePin -> flip pixel_on, then setPixelColor() + pixel->show()
    xTaskNotifyWait / portMAX_DELAY / pdMS_TO_TICKS unchanged — same FreeRTOS API
*/

void vtick_led2(void *pvParameters) {
    //Pass in pointer instead of global solutions
    //Casting the void pointer into an Adafruit_NeoPixel pointer
    Adafruit_NeoPixel* pixelPtr = (Adafruit_NeoPixel*)pvParameters; 

    //Set green as the color for this task
    const uint32_t COLOR_ON  = pixelPtr->Color(0, 150, 0); 

    // LED starts OFF
    pixelPtr->setPixelColor(0, 0);
    pixelPtr->show();

    //Others 
    uint32_t cmd_from_notif;
    bool     pixel_on = false; // tracks current on/off state (replaces GPIO register read)

    for (;;) {

        switch (led2_state) {

            case LED2_ON:
                //If the previous update tells us that the led should be on, we turn it on
				//then we go back to sleep until we receive another state change (xTaskNotifyWait)
                pixelPtr->setPixelColor(0, COLOR_ON); //sets the color of LED
                pixelPtr->show();                     //turns on the LED
                pixel_on = true;                      //update the flag 

                //0 means that it won't clear any of the notification bits before checking whether or not there's a pending notification
				//0xFFFFFFFF means that ALL notification bits will be reset/cleared after checking if there's a pending notif
				//cmd_from_notif copies the value of the notification that woke the task
				//Waits for portMAD_DELAY (forever) until a notification arrives
                xTaskNotifyWait(0, 0xFFFFFFFF, &cmd_from_notif, portMAX_DELAY);

                //once a notification arrives we cast it to the led state (should maybe use a lookup table? or
				// make sure im using only the enum values when giving a notification)
				//we successfully updated the state of the leds
				//the switch statement breaks, it loops again with the updated state
                led2_state = (led2_state_t)cmd_from_notif;
            break;



            case LED2_OFF:
                pixelPtr->setPixelColor(0, 0);
                pixelPtr->show();
                pixel_on = false;

                xTaskNotifyWait(0, 0xFFFFFFFF, &cmd_from_notif, portMAX_DELAY);
                led2_state = (led2_state_t)cmd_from_notif;
                break;

            case LED2_BLINK_SLOW:
                pixel_on = !pixel_on; //set the inverse of whether its ON or OFF for the blink
                pixelPtr->setPixelColor(0, pixel_on ? COLOR_ON : 0); //if it IS in fact ON, we do COLOR_ON otherwise we do no color
                pixelPtr->show(); //show

                // Pretty much same xTaskNotifyWait setup from LED_ON or LED_OFF
                // Now we use pdMS_TO_TICKS(1000), this only has it wait for 1000ms or 1 second
                // If the task wakes (due to the timeout) and there is no pending notification then it returns pdFALSE
                // So we ignore the if block and loop the switch statement to toggle the LED
                // But if we wake due to a notification then it returns pdTRUE we go into the if block
                // Notificaiton is copied, new state is casted and we run the switch case with the new state
                if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd_from_notif, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    led2_state = (led2_state_t)cmd_from_notif;
                }
            break;

            case LED2_BLINK_MEDIUM: //the same as blink slow
                pixel_on = !pixel_on;
                pixelPtr->setPixelColor(0, pixel_on ? COLOR_ON : 0);
                pixelPtr->show();
                if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd_from_notif, pdMS_TO_TICKS(400)) == pdTRUE) {
                    led2_state = (led2_state_t)cmd_from_notif;
                }
            break;

            case LED2_BLINK_FAST: //the same as blink slow
                pixel_on = !pixel_on;
                pixelPtr->setPixelColor(0, pixel_on ? COLOR_ON : 0);
                pixelPtr->show();
                if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd_from_notif, pdMS_TO_TICKS(100)) == pdTRUE) {
                    led2_state = (led2_state_t)cmd_from_notif;
                }
            break;

            default:
                led2_state = LED2_OFF;
            break;

        }//switch

    }//for

}//task
