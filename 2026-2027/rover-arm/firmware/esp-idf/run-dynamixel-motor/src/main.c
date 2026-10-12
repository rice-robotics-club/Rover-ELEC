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

// Globally Define UART Port for RS-485 Communication
const uart_port_t uart_num_rs485 = UART_NUM_1;

// Function Used to Enable Dynamixel Motors for Velocity Control
void enableDynamixel(uint8_t motor_id) {
    // |---- header ----|  ID  |len L,H | inst | addr L,H  | data | CRC L,H |
    uint8_t pkts[3][13] = {
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x00,0x02, 0x00, 0x00,0x00}, // 1. Torque Enable (512) = 0
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x0B,0x00, 0x01, 0x00,0x00}, // 2. Operating Mode (11) = 1 (Velocity)
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x06,0x00, 0x03, 0x00,0x02, 0x01, 0x00,0x00}, // 3. Torque Enable (512) = 1
    };

    for (int p = 0; p < 3; p++) {
        uint8_t *enablePayload = pkts[p];

        // Insert ID and Compute CRC-16 (poly 0x8005) Over Bytes 0..10
        enablePayload[4] = motor_id;
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

void transmitDynamixelPayload(uint8_t motor_id){
    // |---- header ----|  ID  |len L,H | inst | addr L,H  |    data (4 B)      | CRC L,H |
    uint8_t commandPayload[16] =
        {0xFF,0xFF,0xFD,0x00, 0x00, 0x09,0x00, 0x03, 0x28,0x02, 0xBB,0x03,0x00,0x00, 0x00,0x00}; // Goal Velocity (552) = 955 (1 rad/s)

    // Insert ID and Compute CRC-16 (poly 0x8005) Over Bytes 0..13
    commandPayload[4] = motor_id;
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

void controlDynamixelMotors(void *parameters) {
    enableDynamixel(0x04);
    TickType_t xLastWakeTime = xTaskGetTickCount();
   const TickType_t period = pdMS_TO_TICKS(10); //100Hz
   while(1) {
        transmitDynamixelPayload(0x04);
        vTaskDelayUntil(&xLastWakeTime, period);
   }
}

void app_main(void) {
   // Install UART Driver for RS 485
   ESP_ERROR_CHECK(uart_driver_install(uart_num_rs485, 256, 0, 0, NULL, 0));
  
   // Configure UART Port for RS 485
   uart_config_t uart_config_rs485 = {
       .baud_rate = 57600, // Default Dynamixel Baud Rate
       .data_bits = UART_DATA_8_BITS,
       .parity = UART_PARITY_DISABLE,
       .stop_bits = UART_STOP_BITS_1,
       .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
       .source_clk = UART_SCLK_APB,
   };
   ESP_ERROR_CHECK(uart_param_config(uart_num_rs485, &uart_config_rs485));
   // Set RS-485 UART Port Pins - TX: 25, RX: 26, RTS: 27
   ESP_ERROR_CHECK(uart_set_pin(uart_num_rs485, 25, 26, 27, UART_PIN_NO_CHANGE)); 
  
   // Set Hardware to RS-485 Half-Duplex Mode
   ESP_ERROR_CHECK(uart_set_mode(uart_num_rs485, UART_MODE_RS485_HALF_DUPLEX));

   // Configure RX Timeout Threshold
   ESP_ERROR_CHECK(uart_set_rx_timeout(uart_num_rs485, 3));

   // Create Task for Controlling Dynamixel Motor
   xTaskCreatePinnedToCore(
       controlDynamixelMotors,
       "Control Dynamixel Motor Actuators",
       4096,
       NULL,
       5,
       NULL,
       1
   );
}
