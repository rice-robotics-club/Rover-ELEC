//use this sketch to run main communication with arduino and call appropriate move functions

//I2C ADDRESS IS 0x08

  /*Instructions that can be sent by Jetson:
    format: movementcommand,speed (two integers separated by a comma with no space)

    speed = any integer between 0 and 65536.  Higher number = higher speed.
    Note: 30000 is a reasonable speed to start testing.
    message = one of the following integers with the desired corresponding message:
      0 = stop rover
      1 = rover go forward
      2 = rover go backward
      3 = rover turn right
      4 = rover turn left
      5 = motor0 forward
      6 = motor0 backward
      7 = motor0 stop
      8 = motor1 forward
      9 = motor1 backward
      10 = motor1 stop
      11 = motor2 forward
      12 = motor2 backward
      13 = motor2 stop
      14 = motor3 forward
      15 = motor3 backward
      16 = motor3 stop
      17 = motor4 forward
      18 = motor4 backward
      19 = motor4 stop

      Note: see printed PCB for motor definitions.
      motor0 is the drill; motor1--4 are drivetrain
  */


#include <Wire.h>

// ================= CONFIG =================
#define I2C_SLAVE_ADDR 0x08
#define SDA_PIN 18
#define SCL_PIN 19
// ==========================================

//variable definitions
bool newcommand = false;
bool stalled = false;
int currentcommand = -1;
int message = -1;
int speed = 0;

String receivedLine = "";
bool newData = false;

// ===== Movement function declarations =====
void initializeSW();
void r(int motornum, int dutyCycle);
void l(int motornum, int dutyCycle);
void s(int motornum);

void stopAll();
void df(int speed);
void db(int speed);
void tl(int speed);
void tr(int speed);

// ==========================================
// I2C RECEIVE HANDLER (Jetson → ESP32)
void receiveEvent(int numBytes) {
  receivedLine = "";

  while (Wire.available()) {
    char c = Wire.read();
    receivedLine += c;
  }

  receivedLine.trim();

  int comma = receivedLine.indexOf(',');
  if (comma < 0) return;

  int msg = receivedLine.substring(0, comma).toInt();
  int spd = receivedLine.substring(comma + 1).toInt();

  if (msg < 0 || msg > 19) return;
  if (spd < 0) spd = 0;
  if (spd > 65535) spd = 65535;

  message = msg;
  speed = spd;
  newData = true;
}

// I2C REQUEST HANDLER (ESP32 → Jetson)
void requestEvent() {
  String response = "ACK:";
  response += message;
  response += ",";
  response += speed;

  Wire.write((const uint8_t*)response.c_str(), response.length());
}

// ==========================================

void setup() {
  Serial.begin(115200);

  initializeSW();

  // Start I2C as slave on pins 18/19
  Wire.begin(I2C_SLAVE_ADDR, SDA_PIN, SCL_PIN);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  Serial.println("ESP32 I2C SLAVE READY");
}

void loop() {

  if (newData) {
    newData = false;

    Serial.print("Received: ");
    Serial.print(message);
    Serial.print(",");
    Serial.println(speed);

    if (message != -1 && message != currentcommand) {
      currentcommand = message;
      newcommand = true;
    }
  }

  if (newcommand) {
    newcommand = false;

    switch (currentcommand) {
      case 0:  stopAll();    break;
      case 1:  df(speed);    break;
      case 2:  db(speed);    break;
      case 3:  tr(speed);    break;
      case 4:  tl(speed);    break;
      case 5:  r(0, speed);  break;
      case 6:  l(0, speed);  break;
      case 7:  s(0);         break;
      case 8:  r(1, speed);  break;
      case 9:  l(1, speed);  break;
      case 10: s(1);         break;
      case 11: r(2, speed);  break;
      case 12: l(2, speed);  break;
      case 13: s(2);         break;
      case 14: r(3, speed);  break;
      case 15: l(3, speed);  break;
      case 16: s(3);         break;
      case 17: r(4, speed);  break;
      case 18: l(4, speed);  break;
      case 19: s(4);         break;
      default: stopAll();    break;
    }
  }
}