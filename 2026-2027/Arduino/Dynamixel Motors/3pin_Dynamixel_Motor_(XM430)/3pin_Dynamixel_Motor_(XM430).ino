#include <DynamixelShield.h>

const uint8_t DXL_ID = 3;
const uint32_t DXL_BAUD_RATE = 1000000;
const float DXL_PROTOCOL_VERSION = 2.0;

DynamixelShield dxl;

using namespace ControlTableItem;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  dxl.begin(DXL_BAUD_RATE);
  dxl.setPortProtocolVersion(DXL_PROTOCOL_VERSION);

  // Stop here if motor ID 3 does not respond.
  if (!dxl.ping(DXL_ID)) {
    while (true) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }
  }

  // Solid L LED means the motor responded.
  digitalWrite(LED_BUILTIN, HIGH);

  dxl.torqueOff(DXL_ID);
  dxl.setOperatingMode(DXL_ID, OP_POSITION);
  dxl.writeControlTableItem(PROFILE_VELOCITY, DXL_ID, 30);
  dxl.torqueOn(DXL_ID);
}

void loop() {
  dxl.setGoalPosition(DXL_ID, 90.0, UNIT_DEGREE);
  delay(3000);

  dxl.setGoalPosition(DXL_ID, 180.0, UNIT_DEGREE);
  delay(3000);
}