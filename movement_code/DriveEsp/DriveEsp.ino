/*
This code is to allow a user to control the DC motors on the Rice Robotics Rover through keyboard inputs.

In the future, this will be done completely over RF. Control format looks like:
<(Motor to Drive):(Direction):(PWM)>

From top view of Rover

L1 ------ R1
|          |
|          |
|          |
|          |
L2 ------ R2

Few examples:

<A:F:127.5> (all forward, 50% duty cycle)
<A:R:127.5> (all backward, 50% duty cycle)
<TR:*:127.5> (turn right)
<TL:*:127.5> (turn left)

*/

// connect VCC, R_EN, L_EN, on all DC Motor Drivers to ESP32 VIN (5V) Pin
// strapping pins are 0, 2, 5, 12, and 15

const byte numChars = 32;
char receivedChars[numChars];

boolean newCmd = false;

struct DCMotor {
  int rightPWMPin;
  int leftPWMPin;
  int leftSpeed;
  int rightSpeed;
};

DCMotor R1 = {4, 16, 0, 0};
DCMotor R2 = {17, 18, 0, 0};
DCMotor L1 = {19, 21, 0, 0};
DCMotor L2 = {22, 23, 0, 0};

void setup() {
  pinMode(R_P1, OUTPUT);
  pinMode(L_P1, OUTPUT);

  pinMode(R_P2, OUTPUT);
  pinMode(L_P2, OUTPUT);

  pinMode(R_P3, OUTPUT);
  pinMode(L_P3, OUTPUT);

  pinMode(R_P4, OUTPUT);
  pinMode(L_P4, OUTPUT);

  Serial.begin(9600);
  Serial.println("<Arduino is ready>");
}

void loop() {
  listenForCmd();
  execute();
}

void allForward(){
  analogWrite(R_P1, 127.5);
  analogWrite(R_P2, 127.5);
  analogWrite(R_P3, 127.5);
  analogWrite(R_P4, 127.5);
}

void listenForCmd() {
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  while (Serial.available() > 0 && newCmd == false) {
    rc = Serial.read();

    if (recvInProgress == true) {
      if (rc != endMarker) {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= numChars) {
          ndx = numChars - 1;
        }
      } else {
        receivedChars[ndx] = '\0';  // terminate the string
        recvInProgress = false;
        ndx = 0;
        newCmd = true;
      }
    }

    else if (rc == startMarker) {
      recvInProgress = true;
    }
  }
}

void execute() {
  if (newCmd == true) {
    Serial.print("Command Recieved:  ");
    Serial.println(receivedChars);
    if (receivedChars == "<A:F:127.5>"){
      allForward();
    }
      newCmd = false;
  }
}
}
