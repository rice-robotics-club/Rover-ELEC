#include <SerialWombat.h>

// --- IMPORTANT DEFINITIONS ---
int SPEED = 30000;
int PWM_PIN = 2;
int QE1_PIN = 6; 
int QE2_PIN = 7;
int REN_PIN = 1;
int LEN_PIN = 4;

// --- SERIAL WOMBAT CHIP OBJECTS ---
SerialWombatChip sw61;
SerialWombatChip sw62;
SerialWombatChip sw63;
SerialWombatChip sw64;

// Attach PWM and QE specifically to whichever you want sw__
SerialWombatPWM pwm(sw64); 
SerialWombatQuadEnc qeWithPullUps(sw64);

// ESP32 Encoder variables
#define DT GPIO_NUM_33
#define CLK GPIO_NUM_32
int lastStateCLK;
int oldval = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Initializing I2C Bus...");

  Wire.begin(21, 22); 
  Wire.setTimeOut(5000);
  delay(500);

  // --- INITIALIZE ALL CHIPS ---
  // We call begin on all of them so they are recognized on the bus
  if(sw61.begin(Wire, 0x61)) Serial.println("SW 0x61 Found");
  if(sw62.begin(Wire, 0x62)) Serial.println("SW 0x62 Found");
  if(sw63.begin(Wire, 0x63)) Serial.println("SW 0x63 Found");
  if(sw64.begin(Wire, 0x64)) Serial.println("SW 0x64 Found - ACTIVE");

  //begin(pin, starting duty cycle, inverted true/false)
  pwm.begin(PWM, 0, false);
  qeWithPullUps.begin(QE1, QE2);  // Initialize a QE on pins 2 and 3  Change these to different pins (say 18 and 19) on the SW18AB, since 3 is SDA


  //digital enable pins
  sw.pinMode(enableBoth, OUTPUT);
  sw.pinMode(checkStall, INPUT);

  //enable motor
  sw.digitalWrite(enableBoth, LOW);
  //spin motor
  pwm.writeDutyCycle(3000);

  analogWrite(33, 100);
}

void loop() {
  // Only reads encoder from sw62
  int newval = qeWithPullUps.read();
  if(newval != oldval){
    Serial.print("Encoder 0x63: ");
    Serial.println(newval);
    oldval = newval;
  }

  delay(50);
}