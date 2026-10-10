#include <Dynamixel2Arduino.h>

using namespace ControlTableItem;

const uint8_t DXL_ID = 6;
const uint32_t DXL_BAUD = 57600;
const float DXL_PROTOCOL = 2.0;
const uint8_t DXL_DIR_PIN = 2;

// M54-40-S250-R encoder counts per output revolution
const int32_t COUNTS_PER_REVOLUTION = 251417;

// About 5 RPM: 1257 × 0.00397746 RPM
const int32_t MOVEMENT_SPEED_RAW = 1257;

Dynamixel2Arduino dxl(Serial, DXL_DIR_PIN);

int32_t startingPosition = 0;

void errorBlink(uint8_t count)
{
  while (true)
  {
    for (uint8_t i = 0; i < count; i++)
    {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }

    delay(1200);
  }
}

bool moveToPosition(int32_t targetPosition)
{
  if (!dxl.setGoalPosition(
        DXL_ID,
        targetPosition,
        UNIT_RAW))
  {
    return false;
  }

  unsigned long movementStart = millis();

  while (true)
  {
    int32_t currentPosition =
      (int32_t)dxl.getPresentPosition(DXL_ID, UNIT_RAW);

    int32_t positionError = targetPosition - currentPosition;

    // Stop waiting when within approximately 1.4 degrees.
    if (labs(positionError) < 1000)
    {
      return true;
    }

    // Stop and report an error if motion takes over 20 seconds.
    if (millis() - movementStart > 20000)
    {
      dxl.torqueOff(DXL_ID);
      return false;
    }

    delay(20);
  }
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  dxl.begin(DXL_BAUD);
  dxl.setPortProtocolVersion(DXL_PROTOCOL);

  delay(1000);

  // One repeating blink: communication failure.
  if (!dxl.ping(DXL_ID))
  {
    errorBlink(1);
  }

  dxl.torqueOff(DXL_ID);

  // Two repeating blinks: mode change failure.
  if (!dxl.setOperatingMode(
        DXL_ID,
        OP_EXTENDED_POSITION))
  {
    errorBlink(2);
  }

  // Set a slow movement speed.
  dxl.writeControlTableItem(
    GOAL_VELOCITY,
    DXL_ID,
    MOVEMENT_SPEED_RAW
  );

  // Three repeating blinks: torque enable failure.
  if (!dxl.torqueOn(DXL_ID))
  {
    errorBlink(3);
  }

  delay(500);

  startingPosition =
    (int32_t)dxl.getPresentPosition(DXL_ID, UNIT_RAW);

  // Solid LED means setup was successful.
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
  // Rotate exactly 360 degrees in one direction.
  if (!moveToPosition(
        startingPosition + COUNTS_PER_REVOLUTION))
  {
    errorBlink(4);
  }

  delay(1000);

  // Rotate exactly 360 degrees back.
  if (!moveToPosition(startingPosition))
  {
    errorBlink(4);
  }

  delay(1000);
}