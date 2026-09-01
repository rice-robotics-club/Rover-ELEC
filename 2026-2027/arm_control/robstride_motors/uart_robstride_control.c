// WARNING: THIS IMPLEMENTATION IS IN PROGRESS

#include "stdio.h"
#include "stdint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/uart.h"

// ESP32 ID for CAN Communication
const uint32_t hostID = 253;

//Define Struct to Store Command Payload for Robstrides
typedef struct {
    float target;
    float torque;
    float position;
    float velocity;
} robstride_commands_t;
// Create a Global Instance of the Robstride Commands Struct
robstride_commands_t robstrideCommands;

// Define Queue Handle for Robstride Command Payloads
static QueueHandle_t robstrideCommandsQueue = NULL;

//Handler for TWAI Node
twai_node_handle_t twai_node_hdl = NULL;

// CAN Host ID Required for Enabling Robstride Motors
uint32_t buildHostID(uint8_t controlMode, uint32_t hostID, uint32_t targetID) {
    return ((uint32_t)controlMode << 24) | (hostID << 8) | targetID;
}

// CAN Motion Control ID Required for the MIT Motor Control Protocol
uint32_t buildMotionControlID(uint8_t controlMode, uint32_t torque, uint32_t targetID) {
    return (uint32_t)controlMode << 24, | (torque << 8) | targetID;
}

void enableRobstride(uint32_t host, uint32_t target) {
   
    uint8_t payload[8] = {0};
    twai_frame_t enable = {
        .header = {
            .id = buildID(0x03, host, target),    // Communication Type 3 - 0x03
            .ide = true                           //Use 29-Bit Extended ID Format
        }
        .buffer = payload                         //Pointer to Data to Transmit
        .buffer_len = sizeof(payload)             //Length of Data to Transmit
    }

    ESP_ERROR_CHECK(twai_node_transmit(twai_node_hdl, &enable, 0));       // Timeout = 0: returns immediately if queue is full
    ESP_ERROR_CHECK(twai_node_transmit_wait_all_done(twai_node_hdl, 0));  // Return Immediately
}

void transmitRobstridePayload(struct robstride_command_t commands) {
    //Define Velocity as a 16 Bit Integer and Map [-44, 44] to Map [0, 65,536]
    uint16_t = (uint16_t)commands.torque;
    uint16_t pos = (uint16_t)commands.position;
    uint16_t vel = ((uint16_t)commands.velocity + 44)*(65536/88.0);
   
    //Kp and Kd Values
    uint16_t Kp = 0x00;
    uint16_t Kd = 0x00;

    //Define and Pack 8 Byte Payload
    (uint8_t)commandPayload[8];

    commandPayload[0] = (uint8_t)0x00;
    commandPayload[1] = (uint8_t)0x00;
    commandPayload[2] = (uint8_t)((vel >> 8) & 0xFF);
    commandPayload[3] = (uint8_t)(vel & 0xFF);
    commandPayload[4] = (uint8_t)0x00;
    commandPayload[5] = (uint8_t)0x00;
    commandPayload[6] = (uint8_t)((Kd >> 8) & 0xFF);
    commandPayload[7] = (uint8_t)(Kd & 0xFF);

    twai_frame_t run = {
    .header = {
        .id = buildMotionControlID(0x01, 0x00, targetID); // Communication Type 1 - 0x01
        .ide = true;                                      // Use Extended 29-Bit ID Header
    }
        .buffer = commandPayload
        .buffer_len = sizeof(commandPayload);
    }

    ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &run, 0));         // Transmit Frame
    ESP_ERROR_CHECK(twai_node_transmit_wait_all_done(node_hdl, 0)); // Return Immediately
}

void controlRobstrideMotors(void *parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz
   
    //Define 8 Byte Payload
    uint8_t commandPayload[8];

    // Parameters for Robstride Motion Control Commands
    uint32_t target = 0x00;

    // Enable All Robstride Motors
    enableRobstride(hostID, 0x06);
    enableRobstride(hostID, 0x07);
    enableRobstride(hostID, 0x7F);

    while(1) {
        //Receives target motor ID, torque, position, and velocity from UART
        if(xQueueReceive(robstrideCommandsQueue, &robstrideCommands, portMAX_DELAY) == pdTRUE) {
            transmitRobstridePayload(robstrideCommands);
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}


void controlDynamixelMotors(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz
    while(1) {
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

void controlDynamixelServos(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount;
    const TickType_t period = pdMS_TO_TICKS(10) //100Hz
    while(1) {
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

void uartCommunication(void *parameters) {
    // Define UART port and its RX buffer
    const uart_port_t uart_num = UART_NUM_2;
    uint8_t rxBuffer[sizeof(robstride_commands_t)];

    // Initialize instance of robstride commands queue
    robstrideCommandsQueue = xQueueCreate(4, sizeof(robstrideCommands));

    while(1) {
        if(uart_read_bytes(uart_num, (uint8_t*)&robstrideCommands, sizeof(rxBuffer),0) == sizeof(rxBuffer)) {
            memcpy(&robstrideCommands, rxBuffer, sizeof(robstrideCommands));
            xQueueSend(robstrideCommandsQueue, &robstrideCommands, 0);
            printf("Target ID: %d | Torque: %d | Position: %d | Velocity: %d\n",
                robstrideCommands.target, robstrideCommands.torque, robstrideCommands.position, robstrideCommands.velocity);
        }
    }
}

extern "C" void app_main(void) {
   
    //Select One of Three UART Ports
    const uart_port_t uart_num = UART_NUM_2;

    //Configure UART Port
    uart_config_t uart_config {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
        .flags = 0,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

    //Set Pins for UART Communication
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreatePinnedToCore(
        controlRobstrideMotors,
        "Control Robstride Motor Actuators",
        4096,
        NULL,
        10,
        NULL,
        1
    )

    xTaskCreatePinnedToCore(
        controlDynamixelMotors,
        "Control Dynamixel Motor Actuators",
        4096,
        NULL,
        5,
        NULL,
        1
    )

    xTaskCreatePinnedToCore(
        controlDynamixelServos,
        "Control Dynamixel Servo Actuators",
        4096,
        NULL,
        20,
        1
    )

    xTaskCreatePinnedToCore(
        uartCommunication,
        "Interface with External Dev Board Through UART Port",
        4096,
        NULL,
        20,
        0
    )
}


