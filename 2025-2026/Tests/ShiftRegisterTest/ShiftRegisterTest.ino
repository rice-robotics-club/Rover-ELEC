#include "ShiftRegister.h"

const int SER = 13;
const int OE = 25;
const int RCLK = 14;

const int SRCLK = 27;
const int SRCLR = 26;

const int STEP = 18;

ShiftRegister reg(8);

void setup() {
  // put your setup code here, to run once:
  reg.SER_PIN = SER_PIN;
  reg.OE_PIN = OE_PIN;
  reg.SRCLK_PIN = SRCLK_PIN;
  reg.RCLK_PIN = RCLK_PIN;
  reg.SRCLR_PIN = SRCLR_PIN;

  reg.setup();

  char pins[] = {0, 0, 1, 1, 0, 0, 0, 0};
  // for (int i = 0; i < 8; i++)
  //   pins[i] = !pins[i];
  reg.setPins(pins);

  pinMode(STEP, OUTPUT);

}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(STEP, HIGH);
  delay(1);
  digitalWrite(STEP, LOW);
  delay(1);
}
