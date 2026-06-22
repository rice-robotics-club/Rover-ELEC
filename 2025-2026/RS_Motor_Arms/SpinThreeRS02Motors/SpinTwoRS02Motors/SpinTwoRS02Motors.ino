#include "driver/twai.h"

#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

const uint8_t MOTOR_ID1 = 6; //CHANGE THIS ID TO THE ID OF THE MOTOR THAT YOU'RE TRYING TO RUN
const uint8_t MOTOR_ID2 = 127; //CHANGE THIS ID TO THE ID OF THE MOTOR THAT YOU'RE TRYING TO RUN
const uint8_t MOTOR_ID3 = 7; //CHANGE THIS ID TO THE ID OF THE MOTOR THAT YOU'RE TRYING TO RUN
const uint8_t MASTER_ID = 253;

// Protocol Constants [cite: 92, 94]
#define CMD_CONTROL  0x01
#define CMD_FEEDBACK 0x02
#define CMD_ENABLE   0x03
#define CMD_STOP     0x04

void setup() {
  Serial.begin(115200);
  
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_1MBITS(); 
  twai_filter_config_t f_config  = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
    Serial.println("System Ready. Enabling Motor...");
    sendSimpleCommand(CMD_ENABLE, MOTOR_ID1); // Enable the motor 
    sendSimpleCommand(CMD_ENABLE, MOTOR_ID2); // Enable the motor 
    sendSimpleCommand(CMD_ENABLE, MOTOR_ID3); // Enable the motor 
    delay(500);
  }
}

// Builds the 29-bit ID: [Type (5bit)][Host(8bit)][Target(8bit)] [cite: 88, 91]
uint32_t buildID(uint8_t type, uint8_t host, uint8_t target) {
  return ((uint32_t)type << 24) | ((uint32_t)host << 8) | (uint32_t)target;
}

void sendSimpleCommand(uint8_t type, uint8_t motorid) {
  twai_message_t msg = {.extd = 1, .data_length_code = 8};
  msg.identifier = buildID(type, MASTER_ID, motorid);
  for(int i=0; i<8; i++) msg.data[i] = 0;
  twai_transmit(&msg, pdMS_TO_TICKS(10));
}

// Sends a Motion Control Command (Type 1) [cite: 91, 92]
void sendMotionCommand(float target_vel, uint8_t motorid) {
  twai_message_t msg = {.extd = 1, .data_length_code = 8};
  msg.identifier = buildID(CMD_CONTROL, MASTER_ID, motorid);

  // We want to spin at constant velocity, so we set:
  // Kp = 0, Target Angle = 0, Kd = 0.5 (damping), Target Vel = target_vel
  uint16_t q_vel = (target_vel + 44.0) * (65535.0 / 88.0); // Map -44 to 44 -> 0 to 65535 [cite: 92]
  uint16_t q_kd = (0.5) * (65535.0 / 5.0);               // Map 0 to 5.0 -> 0 to 65535 [cite: 92]

  msg.data[0] = 0; msg.data[1] = 0;           // Target Angle (not used if Kp=0)
  msg.data[2] = (q_vel >> 8) & 0xFF;          // Velocity High Byte
  msg.data[3] = q_vel & 0xFF;                 // Velocity Low Byte
  msg.data[4] = 0; msg.data[5] = 0;           // Kp = 0
  msg.data[6] = (q_kd >> 8) & 0xFF;           // Kd High Byte
  msg.data[7] = q_kd & 0xFF;                  // Kd Low Byte

  twai_transmit(&msg, pdMS_TO_TICKS(10));
}

void loop() {
  // Every 100ms, send a motion command to keep the motor spinning
  static uint32_t lastCmd = 0;
  if (millis() - lastCmd > 100) {
    sendMotionCommand(2.0, MOTOR_ID1); // Spin at 2 rad/s
    sendMotionCommand(2.0, MOTOR_ID2); // Spin at 2 rad/s
    sendMotionCommand(2.0, MOTOR_ID3); // Spin at 2 rad/s
    lastCmd = millis();
  }

  twai_message_t rx_msg;
  if (twai_receive(&rx_msg, pdMS_TO_TICKS(5)) == ESP_OK) {
    uint8_t type = (rx_msg.identifier >> 24) & 0x1F;
    if (type == CMD_FEEDBACK) {
      // Corrected Parsing 
      uint16_t r_ang = (rx_msg.data[0] << 8) | rx_msg.data[1];
      uint16_t r_vel = (rx_msg.data[2] << 8) | rx_msg.data[3];
      uint16_t r_trq = (rx_msg.data[4] << 8) | rx_msg.data[5];

      float angle = r_ang * (8.0 * PI / 65535.0) - (4.0 * PI); // -4pi to 4pi 
      float velocity = r_vel * (88.0 / 65535.0) - 44.0;       // -44 to 44 
      float torque = r_trq * (34.0 / 65535.0) - 17.0;         // -17 to 17 

      Serial.printf("Angle: %.2f | Vel: %.2f | Trq: %.2f\n", angle, velocity, torque);
    }
  }
}
