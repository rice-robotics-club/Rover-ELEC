#include "stdio.h"
#include "stdint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"

#include <string.h>
#include <math.h>

// Pins Used to Communicate to CAN Transceiver
#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

// UART Port Connected to the USB Bridge (Laptop)
#define HOST_UART UART_NUM_0

// Number of Robstride Motors
#define NUM_ROBSTRIDES 2

// Time Without a Valid Command Before the Motors are Stopped
#define HOST_TIMEOUT_MS 200

// ESP32 ID for CAN Communication
static const uint32_t hostID = 253;

// Actuator Struct
typedef struct {
    uint32_t motorID;
    uint16_t position;
    float    velocity;
} actuator_t;

// Array of Actuators
static actuator_t ACTUATORS[NUM_ROBSTRIDES] = {
    {0x07, 0, 0},
    {0x7F, 0, 0}
};

// Command Payload Struct
typedef struct {
    uint8_t enable;
    uint8_t forward;
    uint8_t backward;
} commands_t;

static QueueHandle_t commandsQueue = NULL;         // Handle for Commands Queue

static twai_node_handle_t twai_node_hdl = NULL;    // Handle for TWAI Node
static uart_port_t uart_num_rs485 = UART_NUM_1;    // UART Port for RS-485 Communication

// Tick Count of the Last Valid Command, Written by the UART Task and Read by the Control Task
static volatile TickType_t lastRx = 0;














// ================================================= Robstride Motor Control Functions =================================================

// CAN Host ID Required for Enabling Robstride Motors
static uint32_t buildHostID(uint8_t controlMode, uint32_t host, uint32_t motorID) {
    return ((uint32_t)controlMode << 24) | (host << 8) | motorID;
}

// CAN Motion Control ID Required for Operation Control Mode
static uint32_t buildMotionControlID(uint8_t controlMode, uint32_t torque, uint32_t motorID) {
    return ((uint32_t)controlMode << 24) | (torque << 8) | motorID;
}

// Convert Float to Unsigned 16-Bit Integer for Bit Packing Structures, Ensures Value is Clamped to Min and Max Range
static uint16_t floatToUint16(float x, float x_min, float x_max) {
    if (x < x_min) x = x_min;
    if (x > x_max) x = x_max;
    return (uint16_t)(((x - x_min) * 65535.0f / (x_max - x_min)) + 0.5f);
}

// Enable Robstride Motor, Ensure to Keep Payloads, Frames, and Relevant Variables Static
static void enableRobstride(uint32_t host, uint32_t motorID, uint32_t i) {
    // Array of Array's to Hold the Payloads for Each Robstride
    static uint8_t payload[NUM_ROBSTRIDES][8] = {{0}};
    // Array of Structs to Hold the Frames for Each Robstride
    static twai_frame_t enable[NUM_ROBSTRIDES];

    enable[i] = (twai_frame_t){
        .header = {
            .id = buildHostID(0x03, host, motorID),    // Communication Type 3 - 0x03
            .ide = true,                               // Use 29-Bit Extended ID Format
        },
        .buffer = payload[i],                          // Pointer to Data to Transmit
        .buffer_len = sizeof(payload[i]),              // Length of Data to Transmit
    };

    // A Full Transmit Queue or Missing Motor Should Not Reboot the ESP32, So the Return Value is Not Wrapped in ESP_ERROR_CHECK
    twai_node_transmit(twai_node_hdl, &enable[i], 0);
}

// Use Command Payload to Transmit Robstride Motor Control Commands, Ensure to Keep Payloads, Frames, and Relevant Variables Static
// i is the Index of the Motor in the ACTUATORS Array
static void transmitRobstridePayload(uint32_t i, float velocity) {
    static twai_frame_t tx_frame[NUM_ROBSTRIDES];
    static uint8_t payload[NUM_ROBSTRIDES][8];

    // Accelerate to Target Velocity
    if (ACTUATORS[i].velocity < velocity) {
        ACTUATORS[i].velocity = fminf(ACTUATORS[i].velocity + 0.1f, velocity);
    } else if (ACTUATORS[i].velocity > velocity) {
        ACTUATORS[i].velocity = fmaxf(ACTUATORS[i].velocity - 0.1f, velocity);
    }

    // Robstride Payload Commands
    uint16_t vel = floatToUint16(ACTUATORS[i].velocity, -44.0f, 44.0f);
    uint16_t torque = floatToUint16(0.0f, -17.0f, 17.0f);
    uint16_t position = floatToUint16(0.0f, -4.0f * 3.14159265f, 4.0f * 3.14159265f);

    // Kp and Kd Values
    uint16_t Kp = floatToUint16(0.0f, 0.0f, 500.0f);
    uint16_t Kd = floatToUint16(0.5f, 0.0f, 5.0f);

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
            .id = buildMotionControlID(0x01, torque, ACTUATORS[i].motorID),    // Communication Type 1 - 0x01
            .ide = true,                                                       // Use Extended 29-Bit ID Header
        },
        .buffer = payload[i],                                                  // Attach 8 Byte Command Payload
        .buffer_len = sizeof(payload[i]),
    };

    // Transmit TWAI Frame to Robstride Motor
    twai_node_transmit(twai_node_hdl, &tx_frame[i], 0);
}

// Function to Control Robstride Motors, Receives Commands from UART and Transmits to Robstride Motors
static void controlRobstrideMotors(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); // 100Hz

    // Local Copy of the Latest Command, Not Shared With the UART Task
    commands_t commands;
    // Enable Byte From the Previous Cycle, Used to Enable Only Once Per Button Press
    uint8_t prevEnable = 0;

    while (1) {
        // Peek at the Latest Command Without Removing it From the Queue, Ignore it if it is Older Than the Timeout
        if (xQueuePeek(commandsQueue, &commands, 0) == pdTRUE && (xTaskGetTickCount() - lastRx) < pdMS_TO_TICKS(HOST_TIMEOUT_MS)) {
            // Use Bit Masking to Determine which Commands to Send to which Motors
            const uint8_t BIT_MASK = 0x01;
            const uint8_t ENABLE_FLAG = (commands.enable >> 7) & BIT_MASK;

            // Enable Only When the Enable Byte Has Changed
            uint8_t newEnable = (ENABLE_FLAG == 0x01) && (commands.enable != prevEnable);
            prevEnable = commands.enable;

            for (uint32_t i = 0; i < NUM_ROBSTRIDES; i++) {
                // Motors to Enable
                if (newEnable && ((commands.enable >> i) & BIT_MASK)) {
                    enableRobstride(hostID, ACTUATORS[i].motorID, i);
                }

                // Motors to Stop
                if (((commands.forward >> i) & BIT_MASK) == ((commands.backward >> i) & BIT_MASK)) {
                    transmitRobstridePayload(i, 0.0f);
                // Motors to Move Forward
                } else if ((commands.forward >> i) & BIT_MASK) {
                    transmitRobstridePayload(i, 5.0f);
                // Motors to Move Backward
                } else {
                    transmitRobstridePayload(i, -5.0f);
                }
            }
        } else {
            // No Recent Command, Keep Motors Stopped and Reset Enable Tracking
            prevEnable = 0;
            for (uint32_t i = 0; i < NUM_ROBSTRIDES; i++) {
                transmitRobstridePayload(i, 0.0f);
            }
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}




















// ================================================= Dynamixel Motor Control Functions =================================================

// Function Used to Enable Dynamixel Motors for Velocity Control
void enableDynamixel(uint32_t i) {
    // |---- header ----|  ID  |len L,H | inst | addr L,H  | data | CRC L,H |
    uint8_t packets[3][13] = {
        {0xFF,0xFF,0xFD,0x00, 0x00, (uint8_t)ACTUATORS[i].motorID,0x00, 0x03, 0x00,0x02, 0x00, 0x00,0x00}, // 1. Torque Enable (512) = 0
        {0xFF,0xFF,0xFD,0x00, 0x00, (uint8_t)ACTUATORS[i].motorID,0x00, 0x03, 0x0B,0x00, 0x01, 0x00,0x00}, // 2. Operating Mode (11) = 1 (Velocity)
        {0xFF,0xFF,0xFD,0x00, 0x00, (uint8_t)ACTUATORS[i].motorID, 0x03, 0x00,0x02, 0x01, 0x00,0x00}, // 3. Torque Enable (512) = 1
    };

    for (int p = 0; p < 3; p++) {
        uint8_t *enablePayload = packets[p];

        // Insert ID and Compute CRC-16 (poly 0x8005) Over Bytes 0..10
        enablePayload[4] = (uint8_t)ACTUATORS[i].motorID;
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

void transmitDynamixelPayload(uint32_t i) {
    // |---- header ----|  ID  |len L,H | inst | addr L,H  |    data (4 B)      | CRC L,H |
    uint8_t commandPayload[16] =
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x09,0x00, 0x03, 0x28,0x02, 0xBB,0x03,0x00,0x00, 0x00,0x00}; // Goal Velocity (552) = 955 (1 rad/s)

    // Insert ID
    commandPayload[4] = (uint8_t)ACTUATORS[i].motorID;
    
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

    // Local Copy of the Latest Command, Not Shared With the UART Task
    commands_t commands;
    // Enable Byte From the Previous Cycle, Used to Enable Only Once Per Button Press
    uint8_t prevEnable = 0;
    
    while(1) {
        // Peek at the Latest Command Without Removing it From the Queue, Ignore it if it is Older Than the Timeout
        if (xQueuePeek(commandsQueue, &commands, 0) == pdTRUE && (xTaskGetTickCount() - lastRx) < pdMS_TO_TICKS(HOST_TIMEOUT_MS)) {
            // Use Bit Masking to Determine which Commands to Send to which Motors
            const uint8_t BIT_MASK = 0x01;
            const uint8_t ENABLE_FLAG = (commands.enable >> 7) & BIT_MASK;

            // Enable Only When the Enable Byte Has Changed
            uint8_t newEnable = (ENABLE_FLAG == 0x01) && (commands.enable != prevEnable);
            prevEnable = commands.enable;

            for (uint32_t i = 0; i < NUM_ROBSTRIDES; i++) {
                // Motors to Enable
                if (newEnable && ((commands.enable >> i) & BIT_MASK)) {
                    enableRobstride(hostID, ACTUATORS[i].motorID, i);
                }

                // Motors to Stop
                if (((commands.forward >> i) & BIT_MASK) == ((commands.backward >> i) & BIT_MASK)) {
                    transmitRobstridePayload(i, 0.0f);
                // Motors to Move Forward
                } else if ((commands.forward >> i) & BIT_MASK) {
                    transmitRobstridePayload(i, 5.0f);
                // Motors to Move Backward
                } else {
                    transmitRobstridePayload(i, -5.0f);
                }
            }
        } else {
            // No Recent Command, Keep Motors Stopped and Reset Enable Tracking
            prevEnable = 0;
            for (uint32_t i = 0; i < NUM_ROBSTRIDES; i++) {
                transmitRobstridePayload(i, 0.0f);
            }
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}



















// =================== UART COMMUNICATION ===================

// Function to Handle UART Communication, Receives Commands from External Dev Board and Sends to Robstride and Dynamixel Motor Control Task
static void uartCommunication(void *parameters) {
    // Local Struct the UART Bytes are Read Into, Not Shared With the Control Task
    commands_t rxCommands;

    // Local Variable Needed for XOR Checksum
    typedef enum { 
        WAIT_SYNC1,
        WAIT_SYNC2, 
        READ_DATA, 
        READ_CHECKSUM 
    } rxState_t;

    rxState_t state = WAIT_SYNC1;
    uint8_t buffer[3];
    uint8_t count = 0;
    uint8_t byte;

    while (1) {
        // Block Until the Bytes Arrive or the Timeout Expires, so the Task Sleeps Instead of Spinning
        if (uart_read_bytes(HOST_UART, &byte, 1, pdMS_TO_TICKS(20)) != 1) {
            state = WAIT_SYNC1;       // Timeout, Drop Any Partial Frame
            continue;
        }

        // XOR Checksum for UART Port Error Detection
            switch (state) {
        case WAIT_SYNC1:
            if (byte == 0xAA) state = WAIT_SYNC2;
            break;
        case WAIT_SYNC2:
            if (byte == 0x55) { state = READ_DATA; count = 0; }
            else if (byte != 0xAA) state = WAIT_SYNC1;
            break;
        case READ_DATA:
            buffer[count++] = byte;
            if (count == 3) state = READ_CHECKSUM;
            break;
        case READ_CHECKSUM:
            if (byte == (buffer[0] ^ buffer[1] ^ buffer[2])) {
                memcpy(&rxCommands, buffer, sizeof(commands_t));
                xQueueOverwrite(commandsQueue, &rxCommands);
                lastRx = xTaskGetTickCount();
            }
            state = WAIT_SYNC1;
            break;
        }
    }
}

// ====================== MAIN FUNCTION ======================

void app_main(void) {
    // Instance of Commands Queue, Length 1 Holds Only the Latest Command
    commandsQueue = xQueueCreate(1, sizeof(commands_t));    

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





    // Create and Enable TWAI Node for CAN Communication with Robstride Motors
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





    
    // Install UART Driver for Communication
    ESP_ERROR_CHECK(uart_driver_install(HOST_UART, 256, 0, 0, NULL, 0));

    // Select and Configure UART Port for Communication with External Dev Board
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(HOST_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(HOST_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_flush_input(HOST_UART));

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


