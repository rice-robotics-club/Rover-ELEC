#include "stdio.h"
#include "stdint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"

#include <math.h>

// Pins Used to Communicate to CAN Transceiver
#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

// ESP32 ID for CAN Communication
static const uint32_t hostID = 253;

// Array of Actuator ID's
uint32_t ACTUATORS[6] = {0x07, 0x7F};

// Command Payload Struct
typedef struct {
    uint8_t enable;
    uint8_t forward;
    uint8_t backward;
} commands_t;
commands_t commands;

QueueHandle_t commandsQueue = NULL;                // Handle of Commands Queue

static twai_node_handle_t twai_node_hdl = NULL;    // Handle for TWAI Node
static uart_port_t uart_num_rs485 = UART_NUM_1;    // UART Port for RS-485 Communication

// Helper Variable for the transmitRobstridePayload Function
float i = 0.0;

// ================================================= Robstride Motor Control Functions =================================================

// CAN Host ID Required for Enabling Robstride Motors
uint32_t buildHostID(uint8_t controlMode, uint32_t hostID, uint32_t targetID) {
    return ((uint32_t)controlMode << 24) | (hostID << 8) | targetID;
}

// CAN Motion Control ID Required for Operation Control Mode
uint32_t buildMotionControlID(uint8_t controlMode, uint32_t torque, uint32_t targetID) {
    return ((uint32_t)controlMode << 24) | (torque << 8) | targetID;
}

// Convert Float to Unsigned 16-Bit Integer for Bit Packing Structures, Ensures Value is Clamped to Min and Max Range
static uint16_t floatToUint16(float x, float x_min, float x_max) {
    if (x < x_min) x = x_min;
    if (x > x_max) x = x_max;
    return (uint16_t)(((x - x_min) * 65535.0f / (x_max - x_min)) + 0.5f);
}

// Enable Robstride Motor, Ensure to Keep Payloads, Frames, and Relevant Variables Static
void enableRobstride(uint32_t hostID, uint32_t targetID, int i) {
    // Array of Array's to Hold the Payloads for Each Robstride
    static uint8_t payload[2][8] = {{0}};
    // Array of Structs to Hold the Frames for Each Robstride
    static twai_frame_t enable[2];

    enable[i] = (twai_frame_t){
        .header = {
            .id = buildHostID(0x03, hostID, targetID), // Communication Type 3 - 0x03
            .ide = true,                               // Use 29-Bit Extended ID Format
        },
        .buffer = payload[i],                          // Pointer to Data to Transmit
        .buffer_len = sizeof(payload[i]),              // Length of Data to Transmit
    };

    ESP_ERROR_CHECK(twai_node_transmit(twai_node_hdl, &enable[i], 0));
}

// Use Command Payload to Transmit Robstride Motor Control Commands, Ensure to Keep Payloads, Frames, and Relevant Variables Static
void transmitRobstridePayload(uint32_t targetID, uint32_t i, float velocity) {
    static twai_frame_t tx_frame[2];
    
    // Accelerate to Target Velocity
    if (i < velocity) {
        i = fminf(i + 0.1f, velocity);
    }
    else if (i > velocity) {
        i = fmaxf(i - 0.1f, velocity);
    }
    uint16_t vel = floatToUint16(5.0f * i, -44.0f, 44.0f);

    // Robstride Payload Commands
    uint16_t torque = floatToUint16(0.0f, -17.0f, 17.0f);
    uint16_t position = floatToUint16(0.0f, -4.0f * 3.14159265f, 4.0f * 3.14159265f);

    // Kp and Kd Values
    uint16_t Kp = floatToUint16(0.0f, 0.0f, 500.0f);
    uint16_t Kd = floatToUint16(0.5f, 0.0f, 5.0f);

    static uint8_t payload[2][8];
    payload[i][0] = (uint8_t)((position >> 8) & 0xFF);
    payload[i][1] = (uint8_t)(position & 0xFF);
    payload[i][2] = (uint8_t)((vel >> 8) & 0xFF);
    payload[i][3] = (uint8_t)(vel & 0xFF);
    payload[i][4] = (uint8_t)((Kp >> 8) & 0xFF);
    payload[i][5] = (uint8_t)(Kp & 0xFF);
    payload[i][6] = (uint8_t)((Kd >> 8) & 0xFF);
    payload[i][7] = (uint8_t)(Kd & 0xFF);

    // Define TWAI Frame
    tx_frame[i] = (twai_frame_t){
        .header = {
            .id = buildMotionControlID(0x01, torque, targetID),    // Communication Type 1 - 0x01
            .ide = true,                                           // Use Extended 29-Bit ID Header
        },
        .buffer = commandPayload,                                  // Attach 8 Byte Command Payload
        .buffer_len = sizeof(commandPayload),
    };

    // Transmit TWAI Frame to Robstride Motor
    twai_node_transmit(twai_node_hdl, &tx_frame[i], 0);
}

// Function to Control Robstride Motors, Receives Commands from UART and Transmits to Robstride Motors
void controlRobstrideMotors(void *parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz

    while(1) {
        // Receives target Motor ID's, Enable Commands, and Velocity from UART
        if(xQueueReceive(commandsQueue, &commands, portMAX_DELAY) == pdTRUE) {     
            // Use Bit Masking to Determine which Commands to Send to which Motors
            uint8_t BIT_MASK = 0x01;
            for (int i = 0; i < 3; i++) {
                // Motors to Enable
                if ((commands.enable >> i) & BIT_MASK) {
                    enableRobstride(hostID, ACTUATORS[i], i);
                // Motors to Stop
                } else if (((commands.forward >> i) & BIT_MASK) && ((commands.backward >> i) & BIT_MASK)) { 
                    transmitRobstridePayload(ACTUATORS[i], 0.0);
                // Motors to Move Forward
                } else if ((commands.forward >> i) & BIT_MASK) {              
                    transmitRobstridePayload(ACTUATORS[i], 1.0);
                // Motors to Move Backward
                } else ((commands.backward >> i) & BIT_MASK) {                
                    transmitRobstridePayload(ACTUATORS[i], -1.0);
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

/*

// ================================================= Dynamixel Motor Control Functions =================================================

// Function Used to Enable Dynamixel Motors for Velocity Control
void enableDynamixel(uint8_t targetID) {
    // |---- header ----|  ID  |len L,H | inst | addr L,H  | data | CRC L,H |
    uint8_t pkts[3][13] = {
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x00,0x02, 0x00, 0x00,0x00}, // 1. Torque Enable (512) = 0
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x0B,0x00, 0x01, 0x00,0x00}, // 2. Operating Mode (11) = 1 (Velocity)
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x00,0x02, 0x01, 0x00,0x00}, // 3. Torque Enable (512) = 1
    };

    for (int p = 0; p < 3; p++) {
        uint8_t *enablePayload = pkts[p];

        // Insert ID and Compute CRC-16 (poly 0x8005) Over Bytes 0..10
        enablePayload[4] = targetID;
        uint16_t crc = 0;
        for (int i = 0; i < 11; i++) {
            crc ^= (uint16_t)enablePayload[i] << 8;
            for (int b = 0; b < 8; b++)
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
        }
        enablePayload[11] = crc & 0xFF;
        enablePayload[12] = crc >> 8;

        // Transmit and Wait for the 11-byte Status Packet Before Next Write
        uart_write_bytes(uart_num_rs485, enablePayload, 13);
        uart_wait_tx_done(uart_num_rs485, pdMS_TO_TICKS(10));
        uart_flush_input(uart_num_rs485);
        
        uint8_t rx[11];
        uart_read_bytes(uart_num_rs485, rx, sizeof rx, pdMS_TO_TICKS(10));
    }
}

void transmitDynamixelPayload(uint8_t targetID){
    // |---- header ----|  ID  |len L,H | inst | addr L,H  |    data (4 B)      | CRC L,H |
    uint8_t commandPayload[16] =
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x09,0x00, 0x03, 0x28,0x02, 0xBB,0x03,0x00,0x00, 0x00,0x00}; // Goal Velocity (552) = 955 (1 rad/s)

    // Insert ID
    commandPayload[4] = targetID;
    
    // Compute CRC-16 (poly 0x8005) Over Bytes 0..13
    uint16_t crc = 0;
    for (int i = 0; i < 14; i++) {
        crc ^= (uint16_t)commandPayload[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
    }
    commandPayload[14] = crc & 0xFF;
    commandPayload[15] = crc >> 8;

    uart_write_bytes(uart_num_rs485, commandPayload, sizeof(commandPayload));
}

// Function to Control Dynamixel Motors, Receives Commands from UART and Transmits to Dynamixel Motors
void controlDynamixelMotors(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); //100Hz
    
    while(1) {
        // Receive Commands from Command Queue
        if (xQueueReceive(commandsQueue, &commands, portMAX_DELAY) == pdTRUE) {
            // Use Bit Masking to Determine which Commands to Send to Which Motors
            uint8_t BIT_MASK = 0x01;
            for (int i = 3; i < 6; i++) {
                // Motors to Enable
                if ((commands.enable >> i) & BIT_MASK) {
                    enableDynamixel(ACTUATORS[i]);
                }
                // Motors to Move Forward
                if ((commands.forward >> i) & BIT_MASK) {
                    transmitDynamixelPayload(ACTUATORS[i]);
                }
                // Motors to Move Backward
                if ((commands.backward >> i) & BIT_MASK) {
                    transmitDynamixelPayload(ACTUATORS[i]);
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

*/

// =================== UART COMMUNICATION ===================

// Function to Handle UART Communication, Receives Commands from External Dev Board and Sends to Robstride and Dynamixel Motor Control Task
void uartCommunication(void *parameters) {
    // Define UART port and its RX buffer
    const uart_port_t uart_num = UART_NUM_2;
    uint8_t rxBuffer[sizeof(commands_t)];

    while(1) {
        if(uart_read_bytes(uart_num, (uint8_t*)&commands, sizeof(rxBuffer),0) == sizeof(rxBuffer)) {
            xQueueSend(commandsQueue, &commands, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ====================== MAIN FUNCTION ======================

void app_main(void) {
    // Instance of Commands Queue
    commandsQueue = xQueueCreate(4, sizeof(commands)); 

    // Install UART Driver for RS 485
    ESP_ERROR_CHECK(uart_driver_install(uart_num_rs485, 256, 0, 0, NULL, 0));

    // Select and Configure UART Port for RS 485 Communication with Dynamixel Motors
    uart_config_t uart_config_rs485 = {
        .baud_rate = 57600, // Default Dynamixel Baud Rate
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num_rs485, &uart_config_rs485));
    ESP_ERROR_CHECK(uart_set_pin(uart_num_rs485, 25, 26, 27, UART_PIN_NO_CHANGE));     // Set RS-485 UART Port Pins - TX: 25, RX: 26, RTS: 27
    ESP_ERROR_CHECK(uart_set_mode(uart_num_rs485, UART_MODE_RS485_HALF_DUPLEX));       // Set Hardware to RS-485 Half-Duplex Mode
    ESP_ERROR_CHECK(uart_set_rx_timeout(uart_num_rs485, 3));                           // Configure RX Timeout Threshold

    // Install UART Driver for Communication
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 256, 0, 0, NULL, 0));
    
    //Select and Configure UART Port for Communication with External Dev Board
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
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)); // Set UART Port Pins - TX: 17, RX: 16

    //Create and Enable TWAI Node for CAN Communication with Robstride Motors
    twai_onchip_node_config_t node_config = {
        .io_cfg = { 
            .tx = TX_PIN, 
            .rx = RX_PIN 
        },
        .bit_timing = { 
            .bitrate = 1000000 // RS-02 is fixed at 1 Mbps
        }, 
        .tx_queue_depth = 5,
    };
    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &twai_node_hdl));
    ESP_ERROR_CHECK(twai_node_enable(twai_node_hdl));

    // Task for Controlling Robstride Motors
    xTaskCreatePinnedToCore(
        controlRobstrideMotors,
        "Control Robstride Motor Actuators",
        4096,
        NULL,
        10,
        NULL,
        1
    );

    // Task for Controlling Dynamixel Motors
    xTaskCreatePinnedToCore(
        controlDynamixelMotors,
        "Control Dynamixel Motor Actuators",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    // Task for UART Communication with External Dev Board
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


