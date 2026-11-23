#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"

// ====================================================== Configuration ======================================================

// Pins Used to Communicate to CAN Transceiver
#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

// Pins Used to Communicate to RS-485 Transceiver
#define RS485_TX_PIN  25
#define RS485_RX_PIN  26
#define RS485_RTS_PIN 27

// UART Port Connected to the USB Bridge (Laptop)
#define HOST_UART UART_NUM_0

// UART Port Connected to the RS-485 Bus (Dynamixels)
#define DYNAMIXEL_UART UART_NUM_1

// UART Receive Buffer Size in Bytes, Shared by Both UART Ports
#define UART_RX_BUFFER_SIZE 256

// Number of Actuators, Robstrides Come First in ACTUATORS[] Followed by the Dynamixels
#define NUM_ROBSTRIDES 2
#define NUM_DYNAMIXELS 1
#define NUM_ACTUATORS  (NUM_ROBSTRIDES + NUM_DYNAMIXELS)

// Period of the Motor Control Tasks (100Hz)
#define CONTROL_PERIOD_MS 10

// Conversion from rad/s to Dynamixel Goal Velocity Units (0.01 rev/min per Unit)
#define DYNAMIXEL_UNITS_PER_RAD_S 954.93f

// Time Without a Valid Command Before the Motors are Stopped
#define HOST_TIMEOUT_MS 200

// Time to Wait for a Byte From the Host Before Dropping a Partial Frame
#define HOST_READ_TIMEOUT_MS 20

// Sync Bytes at the Start of Every Frame Sent by the Host
#define SYNC_BYTE_1 0xAA
#define SYNC_BYTE_2 0x55

// ESP32 ID for CAN Communication
static const uint32_t hostID = 253;

// ====================================================== Data Structures ======================================================

// Actuator Struct, One Per Robstride or Dynamixel
typedef struct {
    uint32_t motorID;
    uint16_t position;
    float    velocity;
} actuator_t;

// Array of Actuators, the Index of an Actuator is Also its Bit Position in commands_t
static actuator_t ACTUATORS[NUM_ACTUATORS] = {
    {0x07, 0, 0.0f},
    {0x7F, 0, 0.0f}, // 0x7F
    {0x04, 0, 0.0f}
};

// Command Payload Struct
typedef struct {
    uint8_t enable;
    uint8_t forward;
    uint8_t backward;
} commands_t;

// States of the Host Frame Parser
typedef enum {
    WAIT_SYNC1,
    WAIT_SYNC2,
    READ_DATA,
    READ_CHECKSUM
} rxState_t;

static QueueHandle_t commandsQueue = NULL;         // Handle for Commands Queue
static twai_node_handle_t twaiNodeHandle = NULL;   // Handle for TWAI Node

// Tick Count of the Last Valid Command, Written by the UART Task and Read by the Control Tasks
static volatile TickType_t lastRx = 0;

// ====================================================== Robstride Motor Control ======================================================

// CAN Host ID Required for Enabling Robstride Motors
static uint32_t buildHostID(uint8_t controlMode, uint32_t host, uint32_t motorID) {
    return ((uint32_t)controlMode << 24) | (host << 8) | motorID;
}

// CAN Motion Control ID Required for Operation Control Mode
static uint32_t buildMotionControlID(uint8_t controlMode, uint32_t torque, uint32_t motorID) {
    return ((uint32_t)controlMode << 24) | (torque << 8) | motorID;
}

// Convert Float to Unsigned 16-Bit Integer for Bit Packing Structures, Ensures Value is Clamped to Min and Max Range
static uint16_t floatToUint16(float x, float xMin, float xMax) {
    if (x < xMin) x = xMin;
    if (x > xMax) x = xMax;
    return (uint16_t)(((x - xMin) * 65535.0f / (xMax - xMin)) + 0.5f);
}

// Enable Robstride Motor, Ensure to Keep Payloads, Frames, and Relevant Variables Static
// i is the Index of the Actuator in the ACTUATORS Array
static void enableRobstride(uint32_t host, uint32_t motorID, uint32_t i) {
    // Payloads and Frames for Each Actuator, Must Stay Valid Until the Frame is Sent
    static uint8_t enablePayload[NUM_ACTUATORS][8] = {{0}};
    static twai_frame_t enableFrame[NUM_ACTUATORS];

    enableFrame[i] = (twai_frame_t){
        .header = {
            .id = buildHostID(0x03, host, motorID),    // Communication Type 3 - 0x03
            .ide = true,                               // Use 29-Bit Extended ID Format
        },
        .buffer = enablePayload[i],                    // Pointer to Data to Transmit
        .buffer_len = sizeof(enablePayload[i]),        // Length of Data to Transmit
    };

    // A Full Transmit Queue or Missing Motor Should Not Reboot the ESP32, So the Return Value is Not Wrapped in ESP_ERROR_CHECK
    twai_node_transmit(twaiNodeHandle, &enableFrame[i], 0);
}

// Use Command Payload to Transmit Robstride Motor Control Commands, Ensure to Keep Payloads, Frames, and Relevant Variables Static
static void transmitRobstridePayload(uint32_t i, float velocity) {
    // Payloads and Frames for Each Actuator, Must Stay Valid Until the Frame is Sent
    static uint8_t txPayload[NUM_ACTUATORS][8];
    static twai_frame_t txFrame[NUM_ACTUATORS];

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
    uint16_t Kd = floatToUint16(50.0f, 0.0f, 5.0f);

    txPayload[i][0] = (uint8_t)((position >> 8) & 0xFF);
    txPayload[i][1] = (uint8_t)(position & 0xFF);
    txPayload[i][2] = (uint8_t)((vel >> 8) & 0xFF);
    txPayload[i][3] = (uint8_t)(vel & 0xFF);
    txPayload[i][4] = (uint8_t)((Kp >> 8) & 0xFF);
    txPayload[i][5] = (uint8_t)(Kp & 0xFF);
    txPayload[i][6] = (uint8_t)((Kd >> 8) & 0xFF);
    txPayload[i][7] = (uint8_t)(Kd & 0xFF);

    // Define TWAI Frame
    txFrame[i] = (twai_frame_t){
        .header = {
            .id = buildMotionControlID(0x01, torque, ACTUATORS[i].motorID),    // Communication Type 1 - 0x01
            .ide = true,                                                       // Use Extended 29-Bit ID Header
        },
        .buffer = txPayload[i],                                                // Attach 8 Byte Command Payload
        .buffer_len = sizeof(txPayload[i]),
    };

    // Transmit TWAI Frame to Robstride Motor
    twai_node_transmit(twaiNodeHandle, &txFrame[i], 0);
}

// Function to Control Robstride Motors, Reads the Latest Command and Transmits to Robstride Motors
static void controlRobstrideMotors(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);

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

// ====================================================== Dynamixel Motor Control ======================================================

// Function Used to Enable Dynamixel Motors for Velocity Control
// i is the Index of the Actuator in the ACTUATORS Array
static void enableDynamixel(uint32_t i) {
    // |---- header ----|  ID  |len L,H | inst | addr L,H  | data | CRC L,H |
    uint8_t packets[3][13] = {
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x00,0x02, 0x00, 0x00,0x00}, // 1. Torque Enable (512) = 0
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x0B,0x00, 0x01, 0x00,0x00}, // 2. Operating Mode (11) = 1 (Velocity)
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x00,0x02, 0x01, 0x00,0x00}, // 3. Torque Enable (512) = 1
    };

    for (int p = 0; p < 3; p++) {
        uint8_t *packet = packets[p];

        // Insert ID and Compute CRC-16 (poly 0x8005) Over Every Byte Before the CRC
        packet[4] = (uint8_t)ACTUATORS[i].motorID;
        uint16_t crc = 0;
        for (size_t n = 0; n < sizeof(packets[0]) - 2; n++) {
            crc ^= (uint16_t)packet[n] << 8;
            for (int b = 0; b < 8; b++) {
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
            }
        }
        packet[11] = crc & 0xFF;
        packet[12] = crc >> 8;

        // Transmit and Wait for the 11-Byte Status Packet Before the Next Write
        uart_write_bytes(DYNAMIXEL_UART, packet, sizeof(packets[0]));
        uart_wait_tx_done(DYNAMIXEL_UART, pdMS_TO_TICKS(10));
        uart_flush_input(DYNAMIXEL_UART);

        uint8_t statusPacket[11];
        uart_read_bytes(DYNAMIXEL_UART, statusPacket, sizeof(statusPacket), pdMS_TO_TICKS(10));
    }
}

// Transmit Goal Velocity Command to Dynamixel Motor, Velocity is in rad/s (Negative is Reverse)
static void transmitDynamixelPayload(uint32_t i, float velocity) {
    // |---- header ----|  ID  |len L,H | inst | addr L,H  |    data (4 B)      | CRC L,H |
    uint8_t commandPayload[16] =
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x09,0x00, 0x03, 0x28,0x02, 0x00,0x00,0x00,0x00, 0x00,0x00}; // Goal Velocity (552)

    // Insert ID
    commandPayload[4] = (uint8_t)ACTUATORS[i].motorID;

    // Convert rad/s to Units, Stored as a Signed 32-Bit Value (Little Endian, Two's Complement)
    uint32_t goalVelocity = (uint32_t)(int32_t)lroundf(velocity * DYNAMIXEL_UNITS_PER_RAD_S);
    commandPayload[10] = goalVelocity & 0xFF;
    commandPayload[11] = (goalVelocity >> 8) & 0xFF;
    commandPayload[12] = (goalVelocity >> 16) & 0xFF;
    commandPayload[13] = (goalVelocity >> 24) & 0xFF;

    // Compute CRC-16 (poly 0x8005) Over Every Byte Before the CRC
    uint16_t crc = 0;
    for (size_t n = 0; n < sizeof(commandPayload) - 2; n++) {
        crc ^= (uint16_t)commandPayload[n] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
        }
    }
    commandPayload[14] = crc & 0xFF;
    commandPayload[15] = crc >> 8;

    uart_write_bytes(DYNAMIXEL_UART, commandPayload, sizeof(commandPayload));
}

// Function to Control Dynamixel Motors, Reads the Latest Command and Transmits to Dynamixel Motors
static void controlDynamixelMotors(void *parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);

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

            for (uint32_t i = NUM_ROBSTRIDES; i < NUM_ACTUATORS; i++) {
                // Motors to Enable
                if (newEnable && ((commands.enable >> i) & BIT_MASK)) {
                    enableDynamixel(i);
                }

                // Motors to Stop
                if (((commands.forward >> i) & BIT_MASK) == ((commands.backward >> i) & BIT_MASK)) {
                    transmitDynamixelPayload(i, 0.0f);
                // Motors to Move Forward
                } else if ((commands.forward >> i) & BIT_MASK) {
                    transmitDynamixelPayload(i, 1.0f);
                // Motors to Move Backward
                } else {
                    transmitDynamixelPayload(i, -1.0f);
                }
            }
        } else {
            // No Recent Command, Keep Motors Stopped and Reset Enable Tracking
            prevEnable = 0;
            for (uint32_t i = NUM_ROBSTRIDES; i < NUM_ACTUATORS; i++) {
                transmitDynamixelPayload(i, 0.0f);
            }
        }
        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

// ====================================================== UART Communication ======================================================

// Function to Handle UART Communication, Receives Commands from the Host and Sends to the Motor Control Tasks
static void uartCommunication(void *parameters) {
    // Local Struct the UART Bytes are Read Into, Not Shared With the Control Tasks
    commands_t rxCommands;

    // Local Variables Needed for the Frame Parser and XOR Checksum
    rxState_t state = WAIT_SYNC1;
    uint8_t buffer[sizeof(commands_t)];
    uint8_t count = 0;
    uint8_t byte;

    while (1) {
        // Block Until a Byte Arrives or the Timeout Expires, so the Task Sleeps Instead of Spinning
        if (uart_read_bytes(HOST_UART, &byte, 1, pdMS_TO_TICKS(HOST_READ_TIMEOUT_MS)) != 1) {
            state = WAIT_SYNC1;       // Timeout, Drop Any Partial Frame
            continue;
        }

        // Frame Parser: Two Sync Bytes, the Command Bytes, Then an XOR Checksum of the Command Bytes
        switch (state) {
            case WAIT_SYNC1:
                if (byte == SYNC_BYTE_1) state = WAIT_SYNC2;
                break;
            case WAIT_SYNC2:
                if (byte == SYNC_BYTE_2) { state = READ_DATA; count = 0; }
                else if (byte != SYNC_BYTE_1) state = WAIT_SYNC1;
                break;
            case READ_DATA:
                buffer[count++] = byte;
                if (count == sizeof(commands_t)) state = READ_CHECKSUM;
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

// ====================================================== Main Function ======================================================

void app_main(void) {
    // Instance of Commands Queue, Length 1 Holds Only the Latest Command
    commandsQueue = xQueueCreate(1, sizeof(commands_t));

    // Install UART Driver for RS-485
    ESP_ERROR_CHECK(uart_driver_install(DYNAMIXEL_UART, UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));

    // Select and Configure UART Port for RS-485 Communication with Dynamixel Motors
    uart_config_t dynamixelUartConfig = {
        .baud_rate = 57600, // Default Dynamixel Baud Rate
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(DYNAMIXEL_UART, &dynamixelUartConfig));
    ESP_ERROR_CHECK(uart_set_pin(DYNAMIXEL_UART, RS485_TX_PIN, RS485_RX_PIN, RS485_RTS_PIN, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(DYNAMIXEL_UART, UART_MODE_RS485_HALF_DUPLEX));    // Set Hardware to RS-485 Half-Duplex Mode
    ESP_ERROR_CHECK(uart_set_rx_timeout(DYNAMIXEL_UART, 3));                        // Configure RX Timeout Threshold

    // Create and Enable TWAI Node for CAN Communication with Robstride Motors
    twai_onchip_node_config_t twaiNodeConfig = {
        .io_cfg = {
            .tx = CAN_TX_PIN,
            .rx = CAN_RX_PIN
        },
        .bit_timing = {
            .bitrate = 1000000 // Robstride Motors are Fixed at 1 Mbps
        },
        .tx_queue_depth = 5,
    };
    ESP_ERROR_CHECK(twai_new_node_onchip(&twaiNodeConfig, &twaiNodeHandle));
    ESP_ERROR_CHECK(twai_node_enable(twaiNodeHandle));

    // Install UART Driver for Communication with the Host
    ESP_ERROR_CHECK(uart_driver_install(HOST_UART, UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));

    // Select and Configure UART Port for Communication with the Host
    uart_config_t hostUartConfig = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(HOST_UART, &hostUartConfig));
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

    // Task for UART Communication with the Host
    xTaskCreatePinnedToCore(
        uartCommunication,
        "Interface with Host Through UART Port",
        4096,
        NULL,
        5,
        NULL,
        1
    );
}