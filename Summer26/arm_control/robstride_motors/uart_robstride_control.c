// WARNING: THIS IMPLEMENTATION IS IN PROGRESS

#include "stdio.h"
#include "stdint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"

// ESP32 ID for CAN Communication
const uint32_t hostID = 253;

// Pins Used to Communicate to CAN Transceiver
#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

//Define Robstride Command Payload Struct
typedef struct {
    float target;
    float torque;
    float position;
    float velocity;
} robstride_commands_t;
robstride_commands_t robstrideCommands;

// Define Queue for Robstride Command Payloads
static QueueHandle_t robstrideCommandsQueue = NULL;

//Handler for TWAI Node
twai_node_handle_t twai_node_hdl = NULL;

// CAN Host ID Required for Enabling Robstride Motors
uint32_t buildHostID(uint8_t controlMode, uint32_t hostID, uint32_t targetID) {
    return ((uint32_t)controlMode << 24) | (hostID << 8) | targetID;
}

// CAN Motion Control ID Required for the MIT Motor Control Protocol
uint32_t buildMotionControlID(uint8_t controlMode, uint32_t torque, uint32_t targetID) {
    return ((uint32_t)controlMode << 24) | (torque << 8) | targetID;
}

void enableRobstride(uint32_t host, uint32_t target) {
   
    uint8_t payload[8] = {0};
    twai_frame_t enable = {
        .header.id = buildHostID(0x03, host, target),    // Communication Type 3 - 0x03
        .header.ide = true,                              //Use 29-Bit Extended ID Format
        .buffer = payload,                               //Pointer to Data to Transmit
        .buffer_len = sizeof(payload),                   //Length of Data to Transmit
    };

    ESP_ERROR_CHECK(twai_node_transmit(twai_node_hdl, &enable, 0));       // Timeout = 0: returns immediately if queue is full
}

static uint16_t floatToUint16(float x, float x_min, float x_max) {
    if (x < x_min) x = x_min;
    if (x > x_max) x = x_max;
    return (uint16_t)(((x - x_min) * 65535.0f / (x_max - x_min)) + 0.5f);
}

void transmitRobstridePayload(robstride_commands_t commands) {
    //Robstride Payload Commands
    uint16_t targetID = (uint16_t)commands.target;
    uint16_t trq= floatToUint16(0.0f, -17.0f, 17.0f);
    uint16_t pos = floatToUint16(0.0f, -4.0f * 3.14159265f, 4.0f * 3.14159265f);
    uint16_t vel = floatToUint16(10.0 * commands.velocity, -44.0f, 44.0f);
   
    //Kp and Kd Values
    uint16_t Kp = floatToUint16(0.0f, 0.0f, 500.0f);
    uint16_t Kd  = floatToUint16(0.5f, 0.0f, 5.0f);

    //Define and Pack 8 Byte Payload
    uint8_t commandPayload[8];

    commandPayload[0] = (uint8_t)((pos >> 8) & 0xFF);
    commandPayload[1] = (uint8_t)(pos & 0xFF);
    commandPayload[2] = (uint8_t)((vel >> 8) & 0xFF);
    commandPayload[3] = (uint8_t)(vel & 0xFF);
    commandPayload[4] = (uint8_t)((Kp >> 8) & 0xFF);
    commandPayload[5] = (uint8_t)(Kp & 0xFF);
    commandPayload[6] = (uint8_t)((Kd >> 8) & 0xFF);
    commandPayload[7] = (uint8_t)(Kd & 0xFF);

    twai_frame_t run = {
        .header = {
            .id = buildMotionControlID(0x01, trq, 0x06),  // Communication Type 1 - 0x01
            .ide = true,  
        },                                                // Use Extended 29-Bit ID Header
        .buffer = commandPayload,
        .buffer_len = 8, //sizeof(commandPayload)

    };

    twai_node_transmit(twai_node_hdl, &run, 0);
}

void controlRobstrideMotors(void *parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz

    // Enable All Robstride Motors
    enableRobstride(hostID, 0x06);

    while(1) {
        //Receives target motor ID, torque, position, and velocity from UART
        if(xQueueReceive(robstrideCommandsQueue, &robstrideCommands, portMAX_DELAY) == pdTRUE) {
            transmitRobstridePayload(robstrideCommands);
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

/*
void controlDynamixelMotors(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz
    while(1) {
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

void controlDynamixelServos(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz
    while(1) {
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}
*/

void uartCommunication(void *parameters) {
    // Define UART port and its RX buffer
    const uart_port_t uart_num = UART_NUM_2;
    uint8_t rxBuffer[sizeof(robstride_commands_t)];

    while(1) {
        if(uart_read_bytes(uart_num, (uint8_t*)&robstrideCommands, sizeof(rxBuffer),0) == sizeof(rxBuffer)) {
            //memcpy(&robstrideCommands, rxBuffer, sizeof(robstrideCommands));
            xQueueSend(robstrideCommandsQueue, &robstrideCommands, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void) {
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 256, 0, 0, NULL, 0));
    
    //Select and Configure UART Port
    const uart_port_t uart_num = UART_NUM_2;
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    //Create and Enable TWAI Node
    twai_onchip_node_config_t node_config = {
        .io_cfg = { 
            .tx = TX_PIN, 
            .rx = RX_PIN 
        },
        .bit_timing = { 
            .bitrate = 1000000 
        }, // RS-02 is fixed at 1 Mbps
        .tx_queue_depth = 5,
    };
    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &twai_node_hdl));
    ESP_ERROR_CHECK(twai_node_enable(twai_node_hdl));

    // Initialize instance of robstride commands queue
    robstrideCommandsQueue = xQueueCreate(4, sizeof(robstrideCommands));

    xTaskCreatePinnedToCore(
        controlRobstrideMotors,
        "Control Robstride Motor Actuators",
        4096,
        NULL,
        10,
        NULL,
        1
    );

    /*
    xTaskCreatePinnedToCore(
        controlDynamixelMotors,
        "Control Dynamixel Motor Actuators",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        controlDynamixelServos,
        "Control Dynamixel Servo Actuators",
        4096,
        NULL,
        4,
        NULL,
        1
    );
    */

    xTaskCreatePinnedToCore(
        uartCommunication,
        "Interface with External Dev Board Through UART Port",
        4096,
        NULL,
        5,
        NULL,
        1
    );
}





