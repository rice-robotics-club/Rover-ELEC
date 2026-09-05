#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

const uint32_t MOTOR_ID  = 127; //ID of Motor to Run
const uint32_t MASTER_ID = 253; // Arbitrary ID Assigned to Master

#define CMD_MOTION_CONTROL 0x01
#define CMD_ENABLE         0x03

twai_node_handle_t node_hdl = NULL;

uint32_t buildID(uint8_t type, uint32_t host, uint32_t target) {
    return ((uint32_t)type << 24) | (host << 8) | target;
}

void enableMotor(uint32_t target, uint32_t host) {
    uint8_t data[8] = {0};
    twai_frame_t frame = {
        .header = { .id = buildID(CMD_ENABLE, host, target), .ide = true },
        .buffer = data,
        .buffer_len = 8,
    };
    twai_node_transmit(node_hdl, &frame, 0);
}

void sendVelocityCommand(float velocity, uint32_t target) {
    uint16_t q_vel = (uint16_t)((velocity + 44.0f) * (65535.0f / 88.0f)); // -44..44 rad/s
    uint16_t q_kd  = (uint16_t)(0.5f * (65535.0f / 5.0f));                // Kd = 0.5

    uint8_t data[8];
    data[0] = 0; data[1] = 0;                       // target angle (unused, Kp = 0)
    data[2] = (q_vel >> 8) & 0xFF;
    data[3] = q_vel & 0xFF;
    data[4] = 0; data[5] = 0;                       // Kp = 0
    data[6] = (q_kd >> 8) & 0xFF;
    data[7] = q_kd & 0xFF;

    twai_frame_t frame = {
        .header = { .id = buildID(CMD_MOTION_CONTROL, 0 /* torque = 0 goes here, see below */, target), .ide = true },
        .buffer = data,
        .buffer_len = 8,
    };
    twai_node_transmit(node_hdl, &frame, 0);
}

void controlRobstrides(void *parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); // 100Hz

    enableMotor(MOTOR_ID, MASTER_ID);
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        sendVelocityCommand(2.0, MOTOR_ID);
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

extern "C" void app_main(void) {
    twai_onchip_node_config_t node_config = {
        .io_cfg = { .tx = TX_PIN, .rx = RX_PIN },
        .bit_timing = { .bitrate = 1000000 }, // RS-02 is fixed at 1 Mbps
        .tx_queue_depth = 5,
    };
    twai_new_node_onchip(&node_config, &node_hdl);
    twai_node_enable(node_hdl);

    xTaskCreatePinnedToCore(controlRobstrides, "Control Robstride Motors", 4096, NULL, 10, NULL, 1);
}
