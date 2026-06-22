#include <AFMotor.h>

// Right Side
AF_DCMotor M1(1);
AF_DCMotor M4(4);

// Left Side
AF_DCMotor M2(2);
AF_DCMotor M3(3);

void setup()
{
  M1.setSpeed(255);
  M2.setSpeed(255);
  M3.setSpeed(255);
  M4.setSpeed(255);

  M1.run(BACKWARD);
  delay(1000);
  M1.setSpeed(100);
  delay(1000);

  M2.run(FORWARD);
  delay(1000);
  M2.setSpeed(100);
  delay(1000);


  M3.run(BACKWARD);
  delay(1000);
  M3.setSpeed(100);
  delay(1000);

  M4.run(FORWARD);
  delay(1000);
  M4.setSpeed(100);
  delay(1000);
}

void loop()
{
  
}