// Rotary Encoder Inputs
#include "driver/gpio.h"

#define DT GPIO_NUM_33
#define CLK GPIO_NUM_32

#define ENABLE GPIO_NUM_25
#define PWM GPIO_NUM_26

float clicks_per_rotation = 5264;

//for calculating RPM & only printing once every second (to keep the printing process from slowing it down):
unsigned long lastPrint = 0;
unsigned long lastCounter = 0;

float RPM = 0;
int counter = 0;
int currentStateCLK;
int lastStateCLK;
String currentDir = "";
unsigned long lastButtonPress = 0;

void setup() {

  pinMode(ENABLE, OUTPUT);

  pinMode(PWM, OUTPUT);


  // Set encoder pins as inputs
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);

  // Setup Serial Monitor
  Serial.begin(115200);

  // Read the initial state of CLK
  lastStateCLK = gpio_get_level(CLK);

  delay(100);
  Serial.println("Bob");

  //enable motor
  digitalWrite(ENABLE, HIGH);

  //turn motor on
  analogWrite(PWM, 60);
}

void loop() {


  // Read the current state of CLK
  currentStateCLK = gpio_get_level(CLK);

  // If last and current state of CLK are different, then pulse occurred
  // React to only 1 state change to avoid double count
  if (currentStateCLK != lastStateCLK) {


    // If the DT state is different than the CLK state then
    // the encoder is rotating CCW so decrement
    if (gpio_get_level(DT) != currentStateCLK) {
      counter--;
      currentDir = "CCW";
    } else {
      // Encoder is rotating CW so increment
      counter++;
      currentDir = "CW";
    }
    
  }
  
  if ((millis() - lastPrint) > 1000) {
        //RPM = [# of clicks]/[change in time, ms] * 1 rotation/[clicks per rotation] * 1000ms/1s * 60s/1min
        //currently, this is actually rotation per second
        RPM = 1000 * (counter - lastCounter) / (clicks_per_rotation * (millis() - lastPrint));
        Serial.print("Direction: ");
        Serial.print(currentDir);
        Serial.print(" | Counter: ");
        Serial.print(counter);
        Serial.print(" | RPS: ");
        Serial.println(RPM);
        lastPrint = millis();
        lastCounter = counter;
      }

  // Remember last CLK state
  lastStateCLK = currentStateCLK;

  // Put in a slight delay to help debounce the reading

}
