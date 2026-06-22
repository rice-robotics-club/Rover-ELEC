#include <Arduino.h>
#include <ESP32Servo.h>

Servo servo1;
Servo servo2;

#define STEPPER_ID 0x01
#define STEPPER_CMD_MODE 0x06
#define SERVO_CMD_MODE 0x07 // Define the new Servo Mode

// Pins for DRV8825
const int DIR_PIN = 19;
const int STEP_PIN = 18;
const int STEP_DELAY_US = 500; 

// Pins for Servos
const int servo1Pin = 12;
const int servo2Pin = 14;

// Initial Positions
int pos1 = 90;
int pos2 = 90;

void decodeAndMove(uint8_t* data);
void handleServoCommand(uint8_t motor_id, uint8_t* data);

void setup() {
  Serial.begin(115200);     

  // Allocate timers for PWM
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);  

  // Setup Servos
  servo1.setPeriodHertz(50);
  servo1.attach(servo1Pin, 500, 2400); 
  servo2.setPeriodHertz(50);
  servo2.attach(servo2Pin, 500, 2400);

  // Set initial positions
  servo1.write(pos1);
  servo2.write(pos2);         
  
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);

  Serial.println("ESP32 USB Receiver Ready");
}

void loop() {
  // Sync: Look for 0xAA
  while (Serial.available() > 0 && Serial.peek() != 0xAA) {
    Serial.read(); 
  }

  // Need 13 bytes for a full frame
  if (Serial.available() >= 13) {
    uint8_t h1 = Serial.read(); // 0xAA
    uint8_t h2 = Serial.read(); // 0x55
    
    if (h1 == 0xAA && h2 == 0x55) {
      uint8_t mode = Serial.read();
      uint8_t motor_id = Serial.read();
      uint8_t data[8];
      Serial.readBytes(data, 8);
      uint8_t received_chk = Serial.read();

      // Checksum
      uint8_t calculated_chk = mode ^ motor_id;
      for (int i = 0; i < 8; i++) calculated_chk ^= data[i];

      if (calculated_chk == received_chk) {
        // --- ROUTING LOGIC ---
        if (mode == STEPPER_CMD_MODE) {
          decodeAndMove(data);
        }
        else if (mode == SERVO_CMD_MODE) {
          handleServoCommand(motor_id, data);
        }
      }
    }
  }
}

void handleServoCommand(uint8_t motor_id, uint8_t* data) {
  // data[0] contains the signed adjustment (e.g., +5 or -5)
  // We need to cast it to int8_t to handle negative numbers
  int8_t adjustment = (int8_t)data[0]; 
  
  Serial.printf("Servo %d -> Adjust: %d\n", motor_id, adjustment);

  if (motor_id == 10) { // Assuming servo1
    pos1 += adjustment;
    // Constrain to physical limits of the servo (0-180)
    pos1 = constrain(pos1, 0, 180);
    servo1.write(pos1);
    Serial.printf("New Pos1: %d\n", pos1);
  } 
  else if (motor_id == 11) { // Assuming servo2
    pos2 += adjustment;
    pos2 = constrain(pos2, 0, 180);
    servo2.write(pos2);
    Serial.printf("New Pos2: %d\n", pos2);
  }
}

void decodeAndMove(uint8_t* data) {
  uint8_t direction = data[0]; 
  uint16_t steps = (data[1] << 8) | data[2];
  uint16_t speed_delay = (data[3] << 8) | data[4];

  if (speed_delay == 0) speed_delay = 500; 

  digitalWrite(DIR_PIN, direction);
  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(speed_delay);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(speed_delay);
  }
}