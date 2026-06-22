#include "driver/twai.h"

#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

// --- Current Settings ---
const uint8_t CURRENT_MOTOR_ID = 127;
const uint8_t MASTER_ID = 253; // Keeping this the same

// --- New Settings ---
const uint8_t NEW_MOTOR_ID = 6;

#define CMD_SET_ID 0x07

void setup() {
  Serial.begin(115200);
  
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_1MBITS(); 
  twai_filter_config_t f_config  = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
    Serial.println("CAN Online. Changing Motor ID to 7 (Master stays 253)...");
    
    twai_message_t msg = {.extd = 1, .data_length_code = 8};
    
    // RS02 Type 7 Identifier Structure:
    // [Type: 5bit] [New Motor ID: 8bit] [Source/Master ID: 8bit] [Target/Current ID: 8bit]
    uint32_t type = CMD_SET_ID;
    
    msg.identifier = (type << 24) | ((uint32_t)NEW_MOTOR_ID << 16) | ((uint32_t)MASTER_ID << 8) | CURRENT_MOTOR_ID;
    
    // Data field requirements for Type 7
    msg.data[0] = NEW_MOTOR_ID; // New CAN ID
    msg.data[1] = MASTER_ID;    // Keep Master ID at 253
    for(int i=2; i<8; i++) msg.data[i] = 0;

    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
      Serial.println("------------------------------------------------");
      Serial.println("SUCCESS: ID Change command sent!");
      Serial.printf("Motor 127 should now respond as ID %d\n", NEW_MOTOR_ID);
      Serial.printf("Motor will continue to report to Master %d\n", MASTER_ID);
      Serial.println("------------------------------------------------");
      Serial.println("IMPORTANT: Please power cycle the motor now.");
    } else {
      Serial.println("FAILED to send. Check wiring.");
    }
  }
}

void loop() {}