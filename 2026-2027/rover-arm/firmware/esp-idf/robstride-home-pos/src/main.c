#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

// ====================================================== Configuration ======================================================

// Pins Used to Communicate to CAN Transceiver
#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

// Pin Connected to physical Homing Button (Internal Pull-Up)
#define BUTTON_PIN GPIO_NUM_0

#define NUM_ROBSTRIDES 2
#define CONTROL_PERIOD_MS 10

// ESP32 Host ID for CAN Communication
static const uint32_t hostID = 253;

typedef struct {
    uint32_t motorID;
} robstride_t;

// Actuator Configuration Array
static robstride_t ROBSTRIDES[NUM_ROBSTRIDES] = {
    {0x07},
    {0x7F}
};

static twai_node_handle_t twaiNodeHandle = NULL;

// ====================================================== Robstride Motor Control ======================================================

static uint32_t buildHostID(uint8_t controlMode, uint32_t host, uint32_t motorID) {
    return ((uint32_t)controlMode << 24) | (host << 8) | motorID;
}

static uint32_t buildMotionControlID(uint8_t controlMode, uint32_t torque, uint32_t motorID) {
    return ((uint32_t)controlMode << 24) | (torque << 8) | motorID;
}

static uint16_t floatToUint16(float x, float xMin, float xMax) {
    if (x < xMin) x = xMin;
    if (x > xMax) x = xMax;
    return (uint16_t)(((x - xMin) * 65535.0f / (xMax - xMin)) + 0.5f);
}

// Enables a Robstride motor over TWAI/CAN
static void enableRobstride(uint32_t host, uint32_t motorID, uint32_t index) {
    static uint8_t enablePayload[NUM_ROBSTRIDES][8] = {{0}};
    static twai_frame_t enableFrame[NUM_ROBSTRIDES];

    enableFrame[index] = (twai_frame_t){
        .header = {
            .id = buildHostID(0x03, host, motorID), // Communication Type 0x03 (Enable)
            .ide = true,                            // 29-Bit Extended Frame Format
        },
        .buffer = enablePayload[index],
        .buffer_len = sizeof(enablePayload[index]),
    };

    twai_node_transmit(twaiNodeHandle, &enableFrame[index], 0);
}

// Sends target position, velocity limit, and gain commands to Robstride motor
static void sendRobstridePositionCommand(uint32_t index, float targetPosRad, float kpVal, float kdVal) {
    static uint8_t txPayload[NUM_ROBSTRIDES][8];
    static twai_frame_t txFrame[NUM_ROBSTRIDES];

    // Convert values to bit-packed 16-bit unsigned ints accepted by the motor protocol
    uint16_t position = floatToUint16(targetPosRad, -4.0f * 3.14159265f, 4.0f * 3.14159265f);
    uint16_t velocity = floatToUint16(0.0f, -44.0f, 44.0f); // Velocity setpoint = 0 rad/s
    uint16_t torque   = floatToUint16(0.0f, -17.0f, 17.0f); // Feedforward torque = 0 N·m

    // Tuning Gains
    uint16_t Kp = floatToUint16(kpVal, 0.0f, 500.0f);
    uint16_t Kd = floatToUint16(kdVal, 0.0f, 5.0f);

    txPayload[index][0] = (uint8_t)((position >> 8) & 0xFF);
    txPayload[index][1] = (uint8_t)(position & 0xFF);
    txPayload[index][2] = (uint8_t)((velocity >> 8) & 0xFF);
    txPayload[index][3] = (uint8_t)(velocity & 0xFF);
    txPayload[index][4] = (uint8_t)((Kp >> 8) & 0xFF);
    txPayload[index][5] = (uint8_t)(Kp & 0xFF);
    txPayload[index][6] = (uint8_t)((Kd >> 8) & 0xFF);
    txPayload[index][7] = (uint8_t)(Kd & 0xFF);

    txFrame[index] = (twai_frame_t){
        .header = {
            .id = buildMotionControlID(0x01, torque, ROBSTRIDES[index].motorID), // Communication Type 0x01
            .ide = true,
        },
        .buffer = txPayload[index],
        .buffer_len = sizeof(txPayload[index]),
    };

    twai_node_transmit(twaiNodeHandle, &txFrame[index], 0);
}

// Task to handle reading the button and commanding motors to the home position
static void homingTask(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
    
    bool isEnabled = false;

    while (1) {
        // Read GPIO state (active low if using internal pull-up)
        if (gpio_get_level(BUTTON_PIN) == 0) {
            
            // Enable motors once when button is first detected as pressed
            if (!isEnabled) {
                for (uint32_t i = 0; i < NUM_ROBSTRIDES; i++) {
                    enableRobstride(hostID, ROBSTRIDES[i].motorID, i);
                }
                isEnabled = true;
            }

            // Command motors to Home Position (0.0 rad) with stiffness Kp=50.0, Kd=1.0
            for (uint32_t i = 0; i < NUM_ROBSTRIDES; i++) {
                sendRobstridePositionCommand(i, 0.0f, 50.0f, 1.0f);
            }
        } else {
            // Reset state if button is released
            isEnabled = false;
        }

        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

// ====================================================== Main Initialization ======================================================

void app_main(void) {
    // Configure Homing Button Input Pin
    gpio_config_t btnConfig = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btnConfig));

    // Configure TWAI Node for Robstride CAN communication
    twai_onchip_node_config_t twaiNodeConfig = {
        .io_cfg = {
            .tx = CAN_TX_PIN,
            .rx = CAN_RX_PIN
        },
        .bit_timing = {
            .bitrate = 1000000 // 1 Mbps fixed baud rate
        },
        .tx_queue_depth = 5,
    };
    ESP_ERROR_CHECK(twai_new_node_onchip(&twaiNodeConfig, &twaiNodeHandle));
    ESP_ERROR_CHECK(twai_node_enable(twaiNodeHandle));

    // Create Homing Task
    xTaskCreatePinnedToCore(
        homingTask,
        "Homing Task",
        4096,
        NULL,
        10,
        NULL,
        1
    );
}