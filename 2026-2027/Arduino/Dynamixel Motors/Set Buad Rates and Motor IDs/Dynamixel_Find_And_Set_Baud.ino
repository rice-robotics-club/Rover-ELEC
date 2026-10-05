#include <Dynamixel2Arduino.h>

// Arduino Uno + ROBOTIS DYNAMIXEL Shield
#define DXL_SERIAL Serial
#define DXL_DIR_PIN 2
#define STATUS_LED LED_BUILTIN

// The desired baud rate is entered through Serial Monitor when the sketch runs.
uint32_t desiredBaud = 57600;
uint8_t desiredID = 1;

// Common Protocol 2.0 baud rates that an Arduino Uno can test reliably.
const uint32_t SEARCH_BAUDS[] = {
  57600,
  1000000,
  115200,
  9600,
  2000000
};

const uint8_t BAUD_COUNT = sizeof(SEARCH_BAUDS) / sizeof(SEARCH_BAUDS[0]);
const float PROTOCOL_VERSION = 2.0;

Dynamixel2Arduino dxl(DXL_SERIAL, DXL_DIR_PIN);

enum ResultState {
  SEARCHING,
  SUCCESS,
  NOT_FOUND,
  CHANGE_FAILED
};

ResultState result = SEARCHING;
uint8_t foundID = 0;
uint32_t foundBaud = 0;
bool baudCommandAccepted = false;
bool idCommandAccepted = false;

bool isSupportedBaud(uint32_t baud) {
  // These rates work with this scanner on an Arduino Uno. The motor itself
  // must also support the selected rate.
  return baud == 9600UL ||
         baud == 57600UL ||
         baud == 115200UL ||
         baud == 1000000UL ||
         baud == 2000000UL;
}

uint32_t getBaudFromSerialMonitor() {
  DXL_SERIAL.begin(115200);
  DXL_SERIAL.setTimeout(1000);

  DXL_SERIAL.println();
  DXL_SERIAL.println(F("=== DYNAMIXEL ID AND BAUD-RATE TOOL ==="));
  DXL_SERIAL.println(F("Connect ONLY ONE motor."));
  DXL_SERIAL.println(F("Enter one of these baud rates, then press Send:"));
  DXL_SERIAL.println(F("9600, 57600, 115200, 1000000, or 2000000"));

  while (true) {
    if (DXL_SERIAL.available() > 0) {
      uint32_t requestedBaud = DXL_SERIAL.parseInt();

      // Clear the rest of the line from the receive buffer.
      while (DXL_SERIAL.available() > 0) {
        DXL_SERIAL.read();
      }

      if (isSupportedBaud(requestedBaud)) {
        DXL_SERIAL.print(F("Requested new baud rate: "));
        DXL_SERIAL.println(requestedBaud);
        return requestedBaud;
      }

      DXL_SERIAL.println(F("Invalid choice. Enter one of the listed rates:"));
      DXL_SERIAL.println(F("9600, 57600, 115200, 1000000, or 2000000"));
    }

    // Slow flash while waiting for input from Serial Monitor.
    digitalWrite(STATUS_LED, HIGH);
    delay(75);
    digitalWrite(STATUS_LED, LOW);
    delay(425);
  }
}

uint8_t getIDFromSerialMonitor() {
  DXL_SERIAL.println();
  DXL_SERIAL.println(F("Enter the new motor ID (0 through 252), then press Send:"));

  while (true) {
    if (DXL_SERIAL.available() > 0) {
      long requestedID = DXL_SERIAL.parseInt();

      // Clear the rest of the line from the receive buffer.
      while (DXL_SERIAL.available() > 0) {
        DXL_SERIAL.read();
      }

      if (requestedID >= 0 && requestedID <= 252) {
        DXL_SERIAL.print(F("Requested new motor ID: "));
        DXL_SERIAL.println(requestedID);
        return (uint8_t)requestedID;
      }

      DXL_SERIAL.println(F("Invalid ID. Enter a number from 0 through 252:"));
    }

    digitalWrite(STATUS_LED, HIGH);
    delay(75);
    digitalWrite(STATUS_LED, LOW);
    delay(425);
  }
}

void runSwitchCountdown() {
  DXL_SERIAL.println();
  DXL_SERIAL.println(F("Move the shield UART switch from UPLOAD to DYNAMIXEL."));
  DXL_SERIAL.println(F("The search starts in 10 seconds. Do NOT press RESET."));
  DXL_SERIAL.flush();

  for (int seconds = 10; seconds > 0; seconds--) {
    DXL_SERIAL.print(seconds);
    DXL_SERIAL.println(F("..."));
    DXL_SERIAL.flush();
    delay(1000);
  }

  // The switch should now connect the Uno UART to the DYNAMIXEL bus.
  DXL_SERIAL.end();
  delay(100);
}

bool findOneMotor() {
  for (uint8_t baudIndex = 0; baudIndex < BAUD_COUNT; baudIndex++) {
    uint32_t testBaud = SEARCH_BAUDS[baudIndex];

    dxl.begin(testBaud);
    dxl.setPortProtocolVersion(PROTOCOL_VERSION);
    delay(100);

    // Normal DYNAMIXEL IDs are 0 through 252.
    for (uint16_t id = 0; id <= 252; id++) {
      if (dxl.ping((uint8_t)id)) {
        foundID = (uint8_t)id;
        foundBaud = testBaud;
        return true;
      }
    }
  }

  return false;
}

void switchToUSBReporting() {
  // Communication with the motor is finished. The same UART can now be used
  // to display the stored result through USB at 115200 baud.
  DXL_SERIAL.end();
  delay(100);
  DXL_SERIAL.begin(115200);
}

void setup() {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // Begin with the shield UART switch in the UPLOAD position.
  desiredBaud = getBaudFromSerialMonitor();
  desiredID = getIDFromSerialMonitor();
  runSwitchCountdown();

  if (!findOneMotor()) {
    result = NOT_FOUND;
    switchToUSBReporting();
    return;
  }

  // Baud rate is stored in EEPROM, so torque must be disabled first.
  dxl.torqueOff(foundID);
  delay(100);

  // Change and verify the ID while still communicating at the original baud.
  uint8_t activeID = foundID;

  if (foundID == desiredID) {
    idCommandAccepted = true;
  } else {
    idCommandAccepted = dxl.setID(foundID, desiredID);
    delay(300);

    if (idCommandAccepted && dxl.ping(desiredID)) {
      activeID = desiredID;
    } else {
      result = CHANGE_FAILED;
      switchToUSBReporting();
      return;
    }
  }

  if (foundBaud == desiredBaud) {
    baudCommandAccepted = true;
  } else {
    baudCommandAccepted = dxl.setBaudrate(activeID, desiredBaud);
  }

  delay(500);

  // Reconnect at the requested baud rate and verify that the motor responds.
  dxl.begin(desiredBaud);
  dxl.setPortProtocolVersion(PROTOCOL_VERSION);
  delay(200);

  if (idCommandAccepted && baudCommandAccepted && dxl.ping(activeID)) {
    result = SUCCESS;
  } else {
    result = CHANGE_FAILED;
  }

  switchToUSBReporting();
}

void printReport() {
  DXL_SERIAL.println();
  DXL_SERIAL.println(F("=== DYNAMIXEL CONFIGURATION RESULT ==="));

  if (result == SUCCESS) {
    DXL_SERIAL.print(F("Motor found at ID: "));
    DXL_SERIAL.println(foundID);
    DXL_SERIAL.print(F("New motor ID verified: "));
    DXL_SERIAL.println(desiredID);
    DXL_SERIAL.print(F("Original baud rate: "));
    DXL_SERIAL.println(foundBaud);
    DXL_SERIAL.print(F("New baud rate verified: "));
    DXL_SERIAL.println(desiredBaud);
    DXL_SERIAL.println(F("SUCCESS"));
  } else if (result == NOT_FOUND) {
    DXL_SERIAL.println(F("No Protocol 2.0 DYNAMIXEL was found."));
    DXL_SERIAL.println(F("Check motor power, port type, cable, and UART switch."));
  } else if (result == CHANGE_FAILED) {
    DXL_SERIAL.print(F("Motor found at ID: "));
    DXL_SERIAL.println(foundID);
    DXL_SERIAL.print(F("Requested motor ID: "));
    DXL_SERIAL.println(desiredID);
    DXL_SERIAL.print(F("Original baud rate: "));
    DXL_SERIAL.println(foundBaud);
    DXL_SERIAL.print(F("Requested baud rate: "));
    DXL_SERIAL.println(desiredBaud);
    DXL_SERIAL.println(F("The new baud rate could not be verified."));
  }
}

void blinkFailurePattern() {
  if (result == NOT_FOUND) {
    // Continuous fast flashing: no motor found.
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    delay(150);
  } else {
    // Two flashes followed by a pause: baud-rate change failed.
    for (uint8_t i = 0; i < 2; i++) {
      digitalWrite(STATUS_LED, HIGH);
      delay(200);
      digitalWrite(STATUS_LED, LOW);
      delay(200);
    }
    delay(1000);
  }
}

void loop() {
  printReport();

  if (result == SUCCESS) {
    digitalWrite(STATUS_LED, HIGH); // Solid LED means success.
    delay(1000);
  } else {
    blinkFailurePattern();
  }
}
