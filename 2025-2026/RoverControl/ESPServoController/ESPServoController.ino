#include <ESP32Servo.h>

Servo servo1;
Servo servo2;

// Standard PWM Pins
const int servo1Pin = 12;
const int servo2Pin = 14;

// Positions
int pos1 = 90;
int pos2 = 90;

void setup() {
  Serial.begin(57600); // USB Serial
  
  // Allow PWM on these pins
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  
  servo1.setPeriodHertz(50);    // Standard 50hz servo
  servo1.attach(servo1Pin, 500, 2400); // Attach with min/max pulse widths
  
  servo2.setPeriodHertz(50);
  servo2.attach(servo2Pin, 500, 2400);

  servo1.write(pos1);
  servo2.write(pos2);
  
}

void loop() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // Servo 1 Control (Up/Down)
    if (cmd == 'U') {
      pos1 = min(180, pos1 + 1);
      servo1.write(pos1);
    } 
    else if (cmd == 'D') {
      pos1 = max(0, pos1 - 1);
      servo1.write(pos1);
    }
    // Servo 2 Control (Left/Right)
    else if (cmd == 'L') {
      pos2 = min(180, pos2 + 1);
      servo2.write(pos2);
    }
    else if (cmd == 'R') {
      pos2 = max(0, pos2 - 1);
      servo2.write(pos2);
    }
  }
}
