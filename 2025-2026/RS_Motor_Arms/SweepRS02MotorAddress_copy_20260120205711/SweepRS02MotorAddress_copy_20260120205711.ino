#include "driver/twai.h"

// --- Pin Configuration ---
#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

// --- Protocol Constants ---
#define CMD_GET_ID 0x00 // Communication Type 0 

void setup() {
  Serial.begin(115200);

  // Initialize CAN at 1Mbps as per RS02 standard [cite: 15]
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_1MBITS(); 
  twai_filter_config_t f_config  = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
    Serial.println("RS02 ID Scanner Online.");
  }
}

void sendScanRequest(uint8_t motorID, uint8_t masterID) {
  twai_message_t msg = {.extd = 1, .data_length_code = 8};
  // Construct 29-bit ID: Type(0) | MasterID(bits 15-8) | MotorID(bits 7-0) 
  msg.identifier = ((uint32_t)CMD_GET_ID << 24) | ((uint32_t)masterID << 8) | motorID;
  for(int i=0; i<8; i++) msg.data[i] = 0;
  
  twai_transmit(&msg, pdMS_TO_TICKS(5));
}

void loop() {
  Serial.println("\n--- Starting New Sweep ---");
  
  for (int motorID = 0; motorID <= 127; motorID++) {
    // We scan both common Master IDs for every possible motor address
    sendScanRequest(motorID, 1);
    sendScanRequest(motorID, 253);

    twai_message_t rx_msg;
    // Check for a response (Timeout of 10ms for each ID)
    if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
      uint8_t type = (rx_msg.identifier >> 24) & 0x1F;
      uint8_t motorResponseID = (rx_msg.identifier >> 8) & 0xFF; 
      uint8_t masterTargetID = rx_msg.identifier & 0xFF;

      if (type == CMD_GET_ID) {
        Serial.println("************************************");
        Serial.printf("MOTOR DISCOVERED!\n");
        Serial.printf("Motor Address: %d\n", motorResponseID);
        Serial.printf("Reporting to Master: %d\n", masterTargetID);
        
        // Data bytes 0-7 contain the 64-bit MCU unique identifier 
        Serial.print("MCU UID: ");
        for(int i=0; i<8; i++) Serial.printf("%02X", rx_msg.data[i]);
        Serial.println("\n************************************");
        
        delay(1000); // Pause for readability
      }
    }

    if (motorID % 20 == 0) {
      Serial.printf("Scanning ID: %d...\n", motorID);
    }
  }
  
  Serial.println("Sweep complete. Retrying in 5 seconds...");
  delay(5000);
}
